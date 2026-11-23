---
title: "File Mapping: CreateFileMapping and MapViewOfFile"
description: "The second Windows-side chapter mirroring mmap — CreateFileMappingW registers the mapping object and MapViewOfFile attaches the view, and offsets align to the 64 KiB allocation granularity and the failure value is NULL. The classic four experiments run again as usual (closing the mapping handle early leaves the view alive, FILE_MAP_COPY private copies never reach the file, FlushViewOfFile flushes dirty pages only, SetEndOfFile blocks truncation), and five new experiments dig one level deeper: VirtualProtect can only tighten within the view's granted access (upgrading FILE_MAP_READ to RW always yields err=87, and the out parameter written as 1 on failure cannot be trusted), SEC_RESERVE's reserve-then-commit two-phase scheme contrasted with Linux overcommit (a real file handle with SEC_RESERVE silently degrades on Win11 and the file gets zero-extended), PrefetchVirtualMemory pulls pages into the standby list yet spares us no working-set page faults (scanning the prefetched region still takes 8192 faults), the 256 MiB selection benchmark where warm sequential mapping is 2.8x faster and random 5.8x faster but the first-touch round at 93 ms is slower than ReadFile and only repeated visits amortize it, and large pages via SEC_LARGE_PAGES probe the privilege threshold (not granted on this machine, filed as untested). Writing to a read-only view measurably raises 0xC0000005"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 28
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
related:
  - "mmap 内存映射:把文件贴进地址空间"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "结构化异常:SEH 与 VEH"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 内存管理
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/file-io/02-file-mapping.md
  source_hash: 2919af4867bf0341524640fd7f64c9cd296d00c25ed8b4b1d141713411b4ecfc
  translated_at: '2026-11-22T15:37:01+00:00'
  engine: glm-5.3
  token_count: 32519
---
# File Mapping: CreateFileMapping and MapViewOfFile

In the previous chapter we worked our way through HANDLE, and put the synchronous reads and writes of ReadFile and WriteFile through their paces. Before writing this chapter, I went back and tallied the cost of `ReadFile` once: the data travels from the file into the system cache, and we still need one more copy before it reaches the caller's buffer. Read a large file sequentially and that extra copy burns a whole extra pass of memory bandwidth for nothing. Random access fares even worse — every few dozen bytes we touch costs us one system call's worth of latency.

The answer Windows gives comes from the same lineage as Linux's `mmap`, and its name is **file mapping**: the file's contents are attached directly into our own process's address space, we access them with pointers, and there is exactly one copy of the cached pages from start to finish — the whole path achieves zero copy. This chapter is therefore the mirror of the Linux-side [mmap memory mapping](../../linux/file-io/02-mmap-memory-mapping.md). We will call it L02 from here on. We carried the experimental approach over wholesale, and the only layer we swapped is the API. The differences from the Linux side turn out to be more numerous than expected: offset alignment rises from the 4 KiB page to the 64 KiB allocation granularity, the failure value changes from `MAP_FAILED` to NULL, and once a view is created there is a boundary of granted access if you want to change its protection — we will see them one experiment at a time below.

Let us lay out the tools and the environment. The division of labor among the common utilities matches what W01 established: the `unique_handle` skeleton is defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md), the boxed `last_error_code` is defined in [The Error-Handling Paradigm](../../thinking/02-error-paradigm.md), those two Thinking Foundation chapters are their only definitions, while `check_win32` is defined in W01 and we simply pick it up here. The experiments come in two batches: the opening four demos (demo1 through demo4) ran on the Windows machine under `%TEMP%\sysprog-win02\`, using MSYS2 UCRT64 g++ 16.1.0 with the flags `-std=c++23 -Wall -Wextra -static` — static linking is there to be rid of DLL dependencies. The five follow-up experiments (e1 through e5), together with all raw outputs, are archived in the repository under `code/volumn_codes/vol8/systems-programming/windows/file-io/02-file-mapping/`. Their numbering follows the archive subdirectories and is independent of the experiment numbers outside this chapter. What determines the order of appearance is the main text, not the numbers — you will meet e5 already in the SEC_RESERVE section, while e3 and e4 do not step on stage until two sections later. Go by the file names and you will not mistake anyone. The e series was compiled uniformly with `-std=c++20 -Wall -Wextra`, and the benchmark run additionally had `-O2`. The capture date is 2026-10-02, and the machine is still the local Win11 26200 box. How the crash code reads back also depends on the shell that launched the run: the demo series ran inside MSYS2's bash, where we saw Segmentation fault with exit code 139, while the e series ran directly through WSL interop, where `$?` was down to 5 — the low 8 bits of 3221225477 (that number is the unsigned decimal of `0xC0000005`, which we will keep meeting), and for the full code we have to print it from inside the program ourselves.

## Two Steps: Register the Mapping Object, Then Attach the View

What Linux finishes with a single `mmap`, Windows splits into two steps, corresponding to two kernel objects: **`CreateFileMappingW`** takes the file's HANDLE and produces a mapping object (section object), on which we register the page protection and the maximum size. **`MapViewOfFile`** then attaches a range of that object into this process's address space. The documentation puts it plainly: `"Creating a file mapping object does not actually map the view into a process address space"` — the first call is only a registration, and once the second call completes, the address is truly in hand. Each step has its own set of parameters, so let us take them in turn.

`CreateFileMappingW(HANDLE, nullptr, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, nullptr)` hands us six parameters, and the ones that deserve our full attention are `flProtect` and the size. `flProtect` sets the page protection: `PAGE_READONLY` pairs with read-only or copy-on-write views and requires the file handle to have been opened with `GENERIC_READ`. `PAGE_READWRITE` pairs with read-write views, and the handle must then carry `GENERIC_READ|GENERIC_WRITE`. The protection level and the handle's permissions must not fight each other — the same restriction as on the Linux side, where an `O_RDONLY` fd combined with `PROT_WRITE` and `MAP_SHARED` earns an `EACCES`. `MAP_PRIVATE` on the Linux side is exempt from that rule, and the mirror of the exemption is the `FILE_MAP_COPY` we will test below.

We pass the size split into two halves as a 64-bit value. When both halves are 0, the mapping object's size is taken to be the current file size. When the given size exceeds the file, how the fork plays out is decided by the page protection. Here is the documentation's own words: `"If an application specifies a size for the file mapping object that is larger than the size of the actual named file on disk and if the page protection allows write access...the file on disk is increased to match the specified size"`. With a write-capable page protection, it is the file itself that gets stretched. If we create a mapping larger than the file with a read-only page protection, rejection is all we get — the error code measured on the machine is 8 (`ERROR_NOT_ENOUGH_MEMORY`), as seen in `demo1` below. A 0-byte file, given a mapping whose size would compute as 0, is bound to fail, and the error code the documentation names is `ERROR_FILE_INVALID`: `"An attempt to map a file with a length of 0 (zero) fails with an error code of ERROR_FILE_INVALID"`. Look at it from the other direction: give an explicit size together with a write-capable page protection, and even an empty file can be stretched.

The last parameter is the name. Fill in a name and it becomes a **named mapping object**, openable by name from other processes — that is Win32's shared memory. Pass `INVALID_HANDLE_VALUE` as the file handle and the mapping is not backed by any file at all. The system's paging file serves as the backing store. That is the customary recipe for shared memory, and we will come back to it when discussing the section attribute SEC_RESERVE.

`MapViewOfFile(HANDLE, dwDesiredAccess, dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap)` carries the second step. `dwDesiredAccess` must be compatible with the protection registered in the first step. We arrange the three access modes into a table:

| View access | Semantics | Mapping object at least | File handle at least |
| --- | --- | --- | --- |
| FILE_MAP_READ | read-only view, and writing it is an access violation | PAGE_READONLY | GENERIC_READ |
| FILE_MAP_WRITE | read-write view (the name says write only, but it reads too) | PAGE_READWRITE | GENERIC_READ\|GENERIC_WRITE |
| FILE_MAP_COPY | copy-on-write, the mirror of MAP_PRIVATE | PAGE_READONLY suffices | GENERIC_READ |

When the parameters reach the offset, the spot most likely to trip us arrives: Linux only requires `offset` to be a multiple of the page size (4 KiB), while the Windows documentation raises the bar: `"They must also match the virtual memory allocation granularity of the system...To obtain the VirtualAlloc memory allocation granularity of the system, use the GetSystemInfo function"`. What it demands is the **allocation granularity**, whose value on x86/x64 is 64 KiB — sixteen times a page. When the length is 0, the documentation says `"the mapping extends from the specified offset to the end of the file mapping"`: the view is attached from the offset all the way to the end of the mapping object.

::: warning The failure value is NULL, and offsets align to 64 KiB
When CreateFileMappingW and MapViewOfFile fail, both return **NULL** and set GetLastError. When checking for errors, do not carry over the habit from `CreateFileW` in the previous chapter — there the failure value is INVALID_HANDLE_VALUE. If we pass in an offset aligned only to the page (4096, say), MapViewOfFile fails outright, and error code 1132 is exactly `ERROR_MAPPED_ALIGNMENT`. We hard-code neither the page size nor the allocation granularity. We fetch them on the spot with `GetSystemInfo`.
:::

Let us run the first experiment and take in the two-step dance, the granularity, and the handle-closing order all at once (excerpt of `demo1.cpp`, with `make_file` and the output helpers omitted):

```cpp
SYSTEM_INFO si {};
GetSystemInfo(&si);
std::printf("granularity : %lu bytes (page size %lu)\n", si.dwAllocationGranularity, si.dwPageSize);

unique_handle file{check_win32("CreateFileW", CreateFileW, p.c_str(), GENERIC_READ,
                               FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr)};
unique_handle mapping{check_win32("CreateFileMappingW", CreateFileMappingW, file.get(), nullptr,
                                  PAGE_READONLY, 0, 0, nullptr)};   // 长度 0:取当前文件大小
void* base = check_win32("MapViewOfFile", MapViewOfFile, mapping.get(), FILE_MAP_READ, 0, 0, 0);
mapping.reset();  // 映射句柄先关:视图自己持引用,对象不会被拆
std::printf("(A) view     : %.16s\n", static_cast<const char*>(base));
check_win32("UnmapViewOfFile", UnmapViewOfFile, base);

SetLastError(0);  // 只读句柄 + 比文件大 1 MiB 的映射:失败,观察错误码
HANDLE big = CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 1u << 20, nullptr);
std::printf("(B) big map  : handle=%p err=%lu\n", big, GetLastError());
```

```text
$ g++ -std=c++23 -Wall -Wextra -static demo1.cpp -o demo1.exe && ./demo1.exe
granularity : 65536 bytes (page size 4096)
(A) view     : AAAABBBBCCCCDDDD
(B) big map  : handle=0000000000000000 err=8
(B2) big map on RW handle: file size now 1048576 bytes
(C) empty    : handle=0000000000000000 err=1006
```

Row (A) is the most interesting: `mapping.reset()` closed the mapping object's handle, yet the view still read the file's contents out. The documentation vouches for it: `"Mapped views of a file mapping object maintain internal references to the object, and a file mapping object does not close until all references to it are released"`. It turns out the view itself holds a reference to the object, and the object refuses to close until all references are released — so the closing order is ours to arrange. This matches the Linux-side practice of calling `close(fd)` right after the mapping is established. The one counterexample where order does matter is saved for the truncation discussion below.

Put rows (B) and (B2) side by side and we are looking at the measured fork in page-protection behavior: creating a mapping larger than the file with a read-only page protection fails outright with error code 8 (`ERROR_NOT_ENOUGH_MEMORY`). We have already grasped the criterion: the stretching clause demands a `flProtect` with write access, and whether the handle carries `GENERIC_WRITE` is never once mentioned in the clause's wording. So even a handle with write access cannot save an oversized mapping as long as our `flProtect` stays `PAGE_READONLY`. Swap the same experiment to a `GENERIC_READ|GENERIC_WRITE` handle plus `PAGE_READWRITE`, and the 16-byte file was stretched to 1048576 bytes on the spot — not one word of the documentation's stretching clause is hollow. Row (C) tests the 0-byte file, and 1006 is precisely the `ERROR_FILE_INVALID` the documentation names.

## mapped_view: This Chapter's RAII Tool

A view is likewise a resource that must be released once acquired, and every `UnmapViewOfFile` we miss is one leak of address space. Following the series' approach we hand it to RAII: `unique_handle` takes care of `CloseHandle`, `mapped_view` takes care of `UnmapViewOfFile`, and the move-only skeleton is isomorphic to the Linux side's `mapped_region`. The full skeleton is defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md). Here we paste only the Windows-side version:

```cpp
class mapped_view {
public:
    mapped_view() noexcept = default;

    mapped_view(HANDLE mapping, DWORD access, unsigned long long offset = 0,
                std::size_t bytes = 0)
    {
        base_ = static_cast<unsigned char*>(
            check_win32("MapViewOfFile", MapViewOfFile, mapping, access,
                        static_cast<DWORD>(offset >> 32), static_cast<DWORD>(offset), bytes));
    }

    mapped_view(mapped_view&& other) noexcept
        : base_(std::exchange(other.base_, nullptr))
    {
    }

    mapped_view& operator=(mapped_view&& other) noexcept
    {
        if (this != &other) { reset(); base_ = std::exchange(other.base_, nullptr); }
        return *this;
    }

    ~mapped_view() { reset(); }

    unsigned char* get() const noexcept { return base_; }
    void reset() noexcept
    {
        if (base_ != nullptr) {
            ::UnmapViewOfFile(base_);
            base_ = nullptr;
        }
    }

    mapped_view(const mapped_view&) = delete;
    mapped_view& operator=(const mapped_view&) = delete;

private:
    unsigned char* base_ = nullptr;
};
```

Setting it beside `mapped_region`, two spots get easier. The failure value is now NULL, so the pointer branch of `check_win32` works directly — no hand-written `MAP_FAILED` check as in the Linux chapter. And for `UnmapViewOfFile` we pass only the base address, not the length. The system remembers the view's size on its own, saving even the step of recording the rounded-up length.

## FILE_MAP_COPY: Copy-on-Write, Seeing Is Believing

`FILE_MAP_COPY`'s promise is cut from the same cloth as `MAP_PRIVATE`'s, and the documentation's own words: `"When a process writes to a copy-on-write page, the system copies the original page to a new page that is private to the process. The new page is backed by the paging file...The contents of the new page are never written back to the original file and are lost when the view is unmapped."`. We take nothing on faith and run the confrontation following the Linux chapter's script: with a 16-byte `AAAABBBBCCCCDDDD`, we change 4 bytes in the COPY view, then ask the file through a separately opened handle, and open a fresh read-only view to inspect the original. Please note that the file handle in our hand carries only `GENERIC_READ` the whole time, and yet the COPY view remains writable (excerpt of `demo2.cpp`, where `peek_file` is a small wrapper that reads 16 bytes via another handle):

```cpp
unique_handle mapping{check_win32("CreateFileMappingW", CreateFileMappingW, file.get(), nullptr,
                                  PAGE_READONLY, 0, 0, nullptr)};

char* copy_base = static_cast<char*>(
    check_win32("MapViewOfFile", MapViewOfFile, mapping.get(), FILE_MAP_COPY, 0, 0, 0));
std::memcpy(copy_base, "XXXX", 4);  // 写的是 COW 出来的私有副本
std::printf("COPY view   : %.16s\n", copy_base);
std::printf("file        : %s\n", peek_file(p).c_str());

char* ro_base = static_cast<char*>(
    check_win32("MapViewOfFile", MapViewOfFile, mapping.get(), FILE_MAP_READ, 0, 0, 0));
std::printf("READ view   : %.16s\n", ro_base);
```

```text
$ ./demo2.exe
COPY view   : XXXXBBBBCCCCDDDD
file        : AAAABBBBCCCCDDDD
READ view   : AAAABBBBCCCCDDDD
```

The output we received lines up with the Linux chapter: the private view holds the XXXX we wrote, the file has not budged, and the newly opened read-only view reads the original. One more question only Windows invites: if the process crashes, do the dirty pages from copy-on-write pollute the original file? That line — `never written back...lost when the view is unmapped` — is the answer: the private pages are not filed under the original file's name at all, and once the view is unmapped they are gone, and the original file remains intact from beginning to end.

## FlushViewOfFile: When Dirty Pages Reach the Disk

A read-write view (`FILE_MAP_WRITE`) modifies pages in the system cache, so let us separate visibility from durability. In demo3 we open a read-write view with `mapped_view`, write `YYYY` at offset 8, and ask the file through another handle, and at the end we also flush once for contrast (excerpt of `demo3.cpp`, with the remaining helpers are the same as demo2's):

```cpp
unique_handle mapping{check_win32("CreateFileMappingW", CreateFileMappingW, file.get(), nullptr,
                                  PAGE_READWRITE, 0, 0, nullptr)};
mapped_view view{mapping.get(), FILE_MAP_WRITE};
std::memcpy(view.get() + 8, "YYYY", 4);
std::printf("before flush: %s\n", peek_file(p).c_str());   // 不刷,文件也已可见
check_win32("FlushViewOfFile", FlushViewOfFile, view.get(), 0);
std::printf("after flush : %s\n", peek_file(p).c_str());
```

```text
$ ./demo3.exe
before flush: AAAABBBBYYYYDDDD
after flush : AAAABBBBYYYYDDDD
```

The two output lines look identical, yet mean different things. The view and ReadFile share the very same cache, and once we write, the file side can already see it — even before any flush. The documentation has reservations about this consistency and writes `not guaranteed`. On synchronous handles it was observable on this machine, so treat it as a bonus, but do not write code that depends on it.

Since visibility comes free, `FlushViewOfFile` worries about the other thing: when dirty pages reach the disk. It only initiates the write-back of dirty pages and returns without waiting for the physical write to complete. Its position lands between `msync(MS_ASYNC)` and `msync(MS_SYNC)`, and on its own it never reaches the level of `msync(MS_SYNC)`. Nor is what it flushes complete, and the documentation has another sentence we preserve verbatim here: `"The FlushViewOfFile function does not flush the file metadata, and it does not wait to return until the changes are flushed from the underlying hardware disk cache and physically written to disk. To flush all the dirty pages plus the metadata for the file and ensure that they are physically written to disk, call FlushViewOfFile and then call the FlushFileBuffers function."`

According to those words, it neither flushes the file's metadata (length, timestamps) nor waits for the hardware's disk cache. To get what Linux's `fsync` pushes — data plus metadata — or to assemble the wait-to-the-end semantics of `msync(MS_SYNC)`, we must follow the documentation's named pairing and append a `FlushFileBuffers` after `FlushViewOfFile` — the handle-level flush call that does Windows' `fsync` work. Its own semantics and usage are covered in detail in W01. We did not run this two-step combination in this chapter. The basis is the quoted sentence above, while `FlushViewOfFile` appearing alone did pass through our hands in demo3. Each step owns its segment. Miss either one, and a crash can leave things stranded in a half-pushed state.

## VirtualProtect: A View's Protection Has a Boundary of Grant

In the Linux chapter, `mprotect` performed the full routine — downgrade, then upgrade back — with the protection still mutable after the mapping was established. Windows' counterpart is `VirtualProtect`, but on mapped views it carries its own ceiling, and the documentation's own words are laid out here: `"For mapped views, this value must be compatible with the access protection specified when the view was mapped"` — the new protection must be compatible with the access granted by MapViewOfFile. The weight of that sentence is what e1 puts on film. The first route of e1 (ro-write) sets the conditions at their most permissive: the file is opened read-write, the mapping object carries `PAGE_READWRITE`, and only the view itself holds `FILE_MAP_READ`. We then try to upgrade the view to RW:

```cpp
// e1_virtualprotect.cpp(节选):PAGE_READWRITE 映射 + 读写文件 + 只读视图
unique_handle f = make_file(L"sysprog-e1.bin", true);
unique_handle m{CreateFileMappingW(f.get(), nullptr, PAGE_READWRITE, 0, 0, nullptr)};
char* view = (char*)MapViewOfFile(m.get(), FILE_MAP_READ, 0, 0, 0);

DWORD garbage = 0xCCCCCCCC;   // 哨兵:看失败调用动没动出参
BOOL up = VirtualProtect(view, 4096, PAGE_READWRITE, &garbage);
printf("[ro-write] 视图=%p(只读,来自 RW 映射)\n", view);
printf("  先试 VirtualProtect(RO->RW) ret=%d err=%lu 出参=%s(失败时无意义)\n", ...);
fflush(stdout);
view[0] = 'X';                // 硬写:异常在此爆发
```

```text
$ ./e1_virtualprotect.exe ro-write; echo "exit=$?"
[ro-write] 视图=0000020506f00000(只读,来自 RW 映射)
  先试 VirtualProtect(RO->RW) ret=0 err=87 出参=PAGE_NOACCESS(0x01)(失败时无意义)
  硬写 view[0]='X' ...[unhandled-exception] code=0xC0000005
  指令地址 ExceptionAddress = 00007ff7bfde1baa
  ExceptionInformation[0]   = 1  (写访问冲突)
  ExceptionInformation[1]   = 0000020506f00000  (目标数据地址)
exit=5
```

The ret we got is 0, err came back 87 (`ERROR_INVALID_PARAMETER`), and the upgrade was refused. Let us restate the criterion: the mapping object is read-write and the file is open for writing, yet the view's grant alone is read-only, and `VirtualProtect` recognizes exactly that layer. The documentation only says "compatible" without saying which layer is judged. Our measured answer is that the criterion lands on the access granted by MapViewOfFile — not on the mapping object's page protection, still less on the handle's permissions (mixed combinations like a FILE_MAP_COPY view over a read-only mapping we did not test. The blank space is left precisely there, and the archive lists no separate entry). We picked up a piece of corroborating evidence along the way: the sentinel 0xCCCCCCCC was written to 1 (`PAGE_NOACCESS`) by the failed call — the documentation promises nothing about the out parameter on failure. That out parameter is `VirtualProtect`'s fourth parameter `lpflOldProtect` (the old-protection out parameter. The `garbage` acting as sentinel in the excerpt receives it). We cannot trust it from a failed call: in the ro-write route it was stuffed with 1, and another failing call in the archive that never made the main text (the oldprotect route) likewise received a 1. The consequence of writing anyway is the `0xC0000005` on the second line — we will meet it repeatedly in this chapter, and how to catch it is the next chapter's main topic.

In the second route (protect-write) we swap the view to `FILE_MAP_ALL_ACCESS` — the spelling that grants both read and write — and walk the same chain. We step the protection down from RW to RO, then down to NOACCESS, then back up to RW. All three steps succeeded, `lpflOldProtect` faithfully reported the pre-change protection at each step, and after the upgrade back to RW the very same write instruction went through. Reopening the file and reading back the first byte, we read the X we wrote — the change went straight through to the backing file, not through copy-on-write:

```text
$ ./e1_virtualprotect.exe protect-write
[protect-write] 视图=000001f9ddc10000(FILE_MAP_ALL_ACCESS)
  降到 PAGE_READONLY          ret=1 lpflOldProtect=PAGE_READWRITE(0x04)
  降到 PAGE_NOACCESS          ret=1 lpflOldProtect=PAGE_READONLY(0x02)
  升回 PAGE_READWRITE         ret=1 lpflOldProtect=PAGE_NOACCESS(0x01)
  写 view[0]='X' ...
  写入后 view[0]=X
  重开文件读回首字节 = X(0x58,落盘验证)
  [结论] 授权范围内改保护有效;写入直达后备文件,不是写时复制
exit=0
```

(A small lesson from the reopen verification: the original handle was opened exclusively, so we had to close the mapping and the file handle cleanly before reopening, otherwise all we got was a sharing violation and a blank read.) The NOACCESS tier got a supplementary experiment (noaccess-read): after switching over, even the read could not get through, and the exception code was again `0xC0000005` — but the record's info[0] gave 0, distinguishing it from the write conflict's 1. The full story of those fields we leave to the next chapter.

So the engineering posture is this: for the ranges we mean to write, claim the grant up front with `FILE_MAP_ALL_ACCESS`, and `VirtualProtect` we use only to tighten. Downgrading and forbidding access can be done at any time, and upgrading again can never cross the boundary the grant drew. The contrast with the Linux fork is also interesting: mprotect's criterion hangs on the fd's open mode (L02 quoted the man page — adding `PROT_WRITE` to a mapping of a read-only-opened file returns EACCES), while Windows' criterion hangs on the view's grant. The judging position genuinely differs. It is just that the two sides' evidence sits at different levels: on the Windows half we tested it with e1, while on the Linux half L02 offers the man page's admonition and that side's live test has not been done.

## SEC_RESERVE: Reserve First, Commit After — a Two-Phase Scheme

Linux's anonymous mmap defaults to the **overcommit** route: the kernel promises address space far beyond physical memory, and pages are honored only at the moment they are actually touched. Windows splits the same affair into two explicitly named steps: **reserve** registers an address range only, handing over no pages at all. Storage arrives only at the **commit** step. `VirtualAlloc`'s `MEM_RESERVE` and `MEM_COMMIT` are the native entrance to this two-phase scheme, and on the file-mapping side we obtain a reserved-state mapping through the `SEC_RESERVE` section attribute. It must be paired with a paging-file-backed mapping — the route where `INVALID_HANDLE_VALUE` is passed as the file handle — and the customary shape of shared memory is exactly that.

The first route of e2 (probe) creates a 64 MiB `SEC_RESERVE` mapping. We touch no byte at all and merely give it a check-up with `VirtualQuery`:

```text
$ ./e2_sec_reserve.exe probe; echo "exit=$?"
[probe] 保留区状态体检(不访问任何字节)
  SEC_RESERVE 64MiB 映射成功,视图=0000028896020000
  VirtualQuery(区首         +0x000000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x4000000
  VirtualQuery(中部         +0x400000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x3c00000
  VirtualQuery(区尾         +0x3fff000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x1000
exit=0
```

All three probes report State as `MEM_RESERVE`, so the whole 64 MiB is registered. Give the Protect column one more glance: measured as 0, not `PAGE_NOACCESS`. So when judging the reserved state our criterion must be State — do not count on Protect. Now the second route (touch) goes and pokes it directly:

```text
$ ./e2_sec_reserve.exe touch; echo "exit=$?"
  SEC_RESERVE 64MiB 映射成功,视图=000001693c7e0000
  VirtualQuery(写入前      +0x000000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x4000000
  写 base[0]='Z' ...[unhandled-exception] code=0xC0000005
  ExceptionInformation[0]   = 1  (写访问冲突)
  ExceptionInformation[1]   = 000001693c7e0000  (目标数据地址)
exit=5
```

No page-table entry exists at all for reserved pages, and writing a single byte earns us another `0xC0000005`. The route that walks the two-phase scheme to completion is the third (commit): with `VirtualAlloc(MEM_COMMIT)` we commit segment A at the region's head and segment B at 32 MiB, leaving a 31 MiB hole in between:

```text
$ ./e2_sec_reserve.exe commit; echo "exit=$?"
  SEC_RESERVE 64MiB 映射成功,视图=0000018cb5480000
[commit] 两段式:先保留整块,再逐段 VirtualAlloc(MEM_COMMIT)
  段A=[0,1MiB) 段B=[32MiB,33MiB),两段之间 31MiB 保持保留态
  VirtualAlloc(段A, MEM_COMMIT)              -> 0000018cb5480000(ok)
  VirtualAlloc(段B, MEM_COMMIT)              -> 0000018cb7480000(ok)
  VirtualQuery(段A           +0x000000): State=MEM_COMMIT(0x1000)  Protect=PAGE_READWRITE(0x04) RegionSize=0x100000
  VirtualQuery(空洞         +0x1000000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x1000000
  VirtualQuery(段B           +0x2000000): State=MEM_COMMIT(0x1000)  Protect=PAGE_READWRITE(0x04) RegionSize=0x100000
  段A全0x5A/段B全0xA5 写入+回读校验和 = 267386880(期望 267386880)
  VirtualAlloc(空洞, RESERVE|COMMIT)          -> 0000000000000000 err=487(487=ERROR_INVALID_ADDRESS)
  VirtualQuery(空洞2        +0x800000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x1800000
exit=0
```

(The third output line is a label printed by the experiment program itself, and we keep it as is.) After the commit, the State of segments A and B flipped to `MEM_COMMIT` with protection `PAGE_READWRITE`. The read-write read-back checksum 267386880 matched the expected value to the digit, while the hole kept its reserved state. Attempting another reservation over the hole got 487 (`ERROR_INVALID_ADDRESS`) from the system: this stretch of address already has an owner, and registering it a second time is not accepted. This is Windows' version of lazy allocation: we register as large a range as we want, and pages are handed over segment by segment on demand.

The genuine surprise sits in the fourth route (file-backed). My starting expectation was: `SEC_RESERVE` paired with a real file handle, and `CreateFileMappingW` would refuse us outright — the experiment program's comment says as much. The documentation's actual wording is far milder: `"This attribute has no effect for file mapping objects that are backed by executable image files or data files (the hfile parameter is a handle to a file)"` — for data-file-backed mappings, it says the attribute has no effect. What that looked like on Win11 26200, measured:

```text
$ ./e2_sec_reserve.exe file-backed; echo "exit=$?"
[file-backed] SEC_RESERVE 配真文件句柄(文件 16KiB,映射对象 1MiB)
  CreateFileMappingW(真文件 + SEC_RESERVE) -> 成功  err=0
  MapViewOfFile                            -> 000002b64bf20000
  VirtualQuery: State=MEM_COMMIT(0x1000) Protect=PAGE_READWRITE(0x04)
  写 v[0]='F' ... 写入后 v[0]=F <- 文件后备把 SEC_RESERVE 兑现成了已提交
  文件长度:映射前 16384 -> 映射后 1048576(被零扩展到 1MiB = 文件后备兑现)
exit=0
```

Aligning the three accounts: the expected refusal never happened, the documentation says no effect, and in our measurement it lands as an ordinary mapping. The ordinary-mapping routine plays out: the view is directly committed and writable, and an oversized mapping with write-capable protection stretches the file — so the 16 KiB file was zero-extended to 1 MiB. The documentation did not fail, and only my expectation did: the reservation was silently honored. The lesson to record is a single line, but a solid one: trying to pair a real file with `SEC_RESERVE` for lazy allocation does not work — it degrades into an ordinary mapping without a sound, without so much as an error code.

> Belonging to the same section-attribute family is `SEC_LARGE_PAGES` (large pages). e5 probed its threshold in passing: `GetLargePageMinimum()` reports a granularity of 2 MiB, and large pages demand the `SeLockMemoryPrivilege` privilege. The trap here is classic, and the documentation spells it out: `AdjustTokenPrivileges` returns non-zero on success, yet only GetLastError tells us whether the privilege was actually granted. What we measured is ret=1 plus GetLastError=1300 (`ERROR_NOT_ALL_ASSIGNED` — the locally logged-in user simply has not been granted this privilege), and the subsequent `VirtualAlloc(MEM_LARGE_PAGES)` and `CreateFileMappingW(SEC_LARGE_PAGES)` both collected 1314 (`ERROR_PRIVILEGE_NOT_HELD`), while the ordinary-page control allocation succeeded as usual. The large-page path we file as requires-privilege, untested. If you want to enable it, grant it in Group Policy and then log out and back in.

## Prefetching and Yielding Pages: PrefetchVirtualMemory and OfferVirtualMemory

In the Linux chapter, `madvise` was our channel for passing hints to the kernel. On the Windows side the hint-passing entrance is a group of calls. `PrefetchVirtualMemory` manages prefetching, `OfferVirtualMemory` and `ReclaimVirtualMemory` manage yielding and reclaiming pages, and `DiscardVirtualMemory` manages voluntary discarding. They arrived in two waves: Prefetch in Windows 8, and Offer, Reclaim, and Discard in Windows 8.1 Update. The MinGW UCRT64 headers declare them all and we can link directly, and e3's probe cross-checked the addresses taken straight from the headers against `GetProcAddress`, both agree, and no dynamic resolution is needed. What actually trips you is the enum name: the MinGW headers spell it `VmOfferPriorityVeryLow` while the SDK documentation writes `VMOfferPriorityVeryLow` — just the case of that middle m. Copy the code from the docs and it will not compile on MinGW.

The prefetching documentation's position, stated up front: `"The prefetched memory is not added to the target process' working set; it is cached in physical memory"`. Two terms need to be brought in here. The **working set** is the batch of physical pages Windows allots to each process — the pages in active use live there, and the system is reluctant to squeeze them out. Prefetched pages do not enter the working set. They go to the **standby list**: pages whose contents are still in physical memory yet belong to no process's working set, sitting in transition state. When accessed again they take a **soft fault** — the page is already in physical memory, and all that is missing is the step of hanging it back into the page table, and even the disk wait is spared. The documentation only says cached in physical memory without naming where the pages go. The standby-list landing is an inference we supply from general Windows memory management, and that is also how the e3 archive reads it. e3's A/B comparison is designed exactly so: a 64 MiB file mapping, we prefetch the first 32 MiB (region A), leave the latter 32 MiB — region B — as the control, then scan both regions once each, recording page faults and elapsed time:

```cpp
// e3_prefetch_offer.cpp(节选):scan 对 volatile 指针逐字节求和,防优化
WIN32_MEMORY_RANGE_ENTRY rng{view, HALF};              // A 区 = 前 32MiB
BOOL ok = PrefetchVirtualMemory(GetCurrentProcess(), 1, &rng, 0);
unsigned long long sb = scan(view + HALF, HALF);       // B:对照,首触
unsigned long long sa = scan(view, HALF);              // A:已预取
```

```text
$ ./e3_prefetch_offer.exe prefetch warm; echo "exit=$?"
[prefetch warm] 64MiB 文件映射,A=[0,32MiB) 预取,B=[32,64MiB) 对照
  文件普通创建(刚写完,内容大概率还驻留缓存)
  文件就绪,视图=0000021c2a010000
  PrefetchVirtualMemory(A 32MiB) ret=1 耗时=0.002s 缺页增量=22 进程读IO增量=0 MiB
  扫 B(未预取,首触)     校验和=3942645760 耗时=0.026s 付缺页=8208 读IO增量=0 MiB
  扫 A(已预取)         校验和=3674210304 耗时=0.026s 付缺页=8192 读IO增量=0 MiB
exit=0
```

Under a warm cache the two regions' elapsed times are identical — 0.026 s on one side and the same 0.026 s on the other — with faults both in the neighborhood of 8192. Prefetching did not spare us the working-set faults: scanning the prefetched region still hangs the pages in one by one. What was saved is the disk round trip, while the fault count was not reduced one bit. When the file is already in the cache, prefetching naturally saves nothing at all. Only the cold regime shows a gap: we rebuilt the same file with `FILE_FLAG_NO_BUFFERING` (reads and writes under it bypass the system cache and go straight to disk) and ran again — scanning B took 0.034 s, scanning A only 0.024 s, A about thirty percent faster than B. Mind the magnitude of that number: this machine's NVMe drive is fast to begin with, the disk-round-trip share that prefetching can save is small here, and this is all the observable ceiling — we file it as measured and skip the bragging multiples. One small observation more: the process's read-IO counters see nothing of a file mapping's paging. Both regions recorded 0.

On the yielding side (e3's offer route) we switch to private memory for the demonstration: 32 MiB committed with `VirtualAlloc`, filled with a checksum pattern, then offered out and reclaimed back, eight rounds in a row:

```text
$ ./e3_prefetch_offer.exe offer; echo "exit=$?"
[offer] 私有内存 32MiB:offer(VeryLow) -> reclaim 轮次 + 主动 Discard
  填充完毕:工作集=36 MiB 缺页=9526
  轮次1:offer ret=0(0=ERROR_SUCCESS) offer后工作集=4 MiB reclaim ret=0(0=内容保留) 模式校验=完好
  轮次2:offer ret=0(0=ERROR_SUCCESS) offer后工作集=4 MiB reclaim ret=0(0=内容保留) 模式校验=完好
  (轮次 3 到 7 与上两行相同,删节)
  轮次8:offer ret=0(0=ERROR_SUCCESS) offer后工作集=4 MiB reclaim ret=0(0=内容保留) 模式校验=完好
  8 轮统计:内容保留 8 轮 / 被改写 0 轮(丢弃需要系统内存压力,单机不硬造)
  DiscardVirtualMemory ret=0(0=ERROR_SUCCESS,立即丢弃) 之后读 8192 页中全零页=8192
exit=0
```

After the offer we watched the working set plunge from 36 MiB straight down to 4 MiB — the most stable observable quantity in the whole table. The content checksums of all eight reclaims came back intact, but we must carry the documentation's reservation with us: `"The data in reclaimed pages may have been discarded, in which case the contents of the memory region is undefined and must be rewritten by the application"` — a successful reclaim does not guarantee the content is still there, and under memory pressure the system really can drop it. Fortunately the reclaim return code keeps the two outcomes apart: `ERROR_SUCCESS` reports reclaimed with content intact, `ERROR_BUSY` reports reclaimed but content dropped. The `reclaim ret=0(0=内容保留)` that e3 prints each round uses exactly that distinction, and all eight rounds got the former. The discard path needs genuine pressure to walk, and on a single machine we do not fake it, and we honestly record 8/8. `DiscardVirtualMemory`, by contrast, is deterministic self-discard: after calling it, reading back all 8192 pages yielded nothing but zeros. Lined up against the Linux side, `PrefetchVirtualMemory` sits where `MADV_WILLNEED` sits, Offer's manner of leaving the trade-off to the system resembles `MADV_FREE`, and the all-zeros read-back after Discard matches the private-anonymous zeroing behavior of `MADV_DONTNEED` as L02 relayed from the man page. One further documentation detail worth recording: offered pages are never written to the paging file. The MEM_RESET family's forgetful resets (the group VirtualAlloc provides) are nowhere near as clean — pages are merely marked discardable and their content is not preserved.

## ReadFile or Mapping: The Timing Table for One 256 MiB File

Now we make good on the comparison owed since the opening. e4 takes the same 256 MiB file and has ReadFile and the mapping each run a full sequential read and a random 4 KiB ×10000 pass, plus a cache-bypassing cold-read control. Each scenario runs 3 rounds and we take the median. Timing uses `QueryPerformanceCounter` (Windows' high-resolution monotonic timer). Content is generated deterministically from page numbers, and both methods' checksums must agree — the sequential scenario's 34225520640 and the random scenario's 5237682176 all matched, so we were indeed reading the same data.

The machine's particulars, stated before the numbers: the local Win11 26200 box — the very machine that appeared in W01 (Ryzen 7 9700X, with the backing drive a WD_BLACK SN7100 NVMe, the drive model recorded by PowerShell's `Get-PhysicalDisk`) — with 61.7 GiB of physical memory and about 35 GiB available at idle. Under a warm cache the whole table runs at memory speed. On a memory-tight machine do not expect to reproduce the same warm numbers. This posture follows the same tradition as the Linux-side L02 two-machine benchmark: the standing rule there was that the numbers hold only on the two machines that produced the benchmark, and our numbers likewise hold only on this machine — the relative relationships carry more reference value than the absolute figures. This compile added `-O2`.

| Scenario | Median | Bandwidth / per-op | Notes |
| --- | --- | --- | --- |
| ReadFile sequential full read (1 MiB blocks) | 51.08 ms | 5012 MiB/s | warm cache |
| MapViewOfFile sequential scan | 18.18 ms | 14081 MiB/s | warm, round 1 includes the 93.45 ms first touch |
| ReadFile random 4 KiB ×10000 | 31.93 ms | 3.19 µs/op | warm cache, SetFilePointerEx plus ReadFile, two system calls per hop |
| MapViewOfFile random 4 KiB ×10000 | 5.54 ms | 0.55 µs/op | warm, round 1 includes the 15.27 ms first touch |
| ReadFile sequential (NO_BUFFERING) | 82.20 ms | 3114 MiB/s | cache-bypassing cold-read control, slowest in the table |

The sequential tier's skeleton code is easiest to take in with both sides pasted side by side (excerpt of `e4_readfile_vs_map.cpp`):

```cpp
// readfile-seq:1MiB 块循环读,页缓存到用户缓冲一次拷贝
for (size_t off = 0; off < kFileSize; off += (1 << 20)) {
    DWORD rd_ = 0;
    ReadFile(f, blk, 1 << 20, &rd_, nullptr);
    got += sum_buf(blk, 1 << 20);
}

// map-seq:视图逐字节扫,首触每页缺页,零拷贝
HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
unsigned char* view = (unsigned char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
got = sum_buf(view, kFileSize);
```

```text
$ ./e4_readfile_vs_map.exe map-seq; echo "exit=$?"
[map-seq] MapViewOfFile 顺序扫全文件,3 轮(轮1含首触缺页,轮2/3暖)
  map-seq 轮1    93.45 ms     2739 MiB/s
  map-seq 轮2    18.18 ms    14081 MiB/s
  map-seq 轮3    18.03 ms    14198 MiB/s
  map-seq 中位    18.18 ms    14081 MiB/s  校验和=34225520640(与理论一致)
exit=0
(readfile-seq / readfile-rand / map-rand / readfile-nobuf 四场的完整输出在存档,数字见表)
```

The warm sequential mapping ran 2.8x faster (18 vs 51 ms), winning precisely the one kernel-to-user-buffer copy it skips. The random 4 KiB mapping ran 5.8x faster (0.55 vs 3.19 µs/op), winning on pointer-as-access versus the two system calls of seek plus read — and the more fragmented the access pattern, the wider the mapping's advantage opens. What truly deserves a second look is map-seq's round 1: 93.45 ms, slower than any round of ReadFile's. Even if all 65536 pages we touch are soft faults (the file is in the cache), each fault amortized to about 1.4 µs over the round. That 93.45 ms of course also contains the roughly 18 ms warm cost of the scan itself. Subtract it and the net fault overhead is about 75 ms, a marginal cost of about 1.15 µs per page — the magnitude holds, and the first-touch round's overhead is real. A mapping's cost is amortized only by revisits: read the same data once and be done, and the mapping actually loses. The more revisits and the more fragmented the access, the more solidly the mapping's advantage stands. The cold-read control tells the same story: bypassing the cache, ReadFile's full read took 82.20 ms — the same order as the 93 ms first touch. One path genuinely hauls data off the disk while the other merely fills in page tables. In the cold regime the gap between the two ways of reading is nowhere near as dramatic as in the warm one.

We also lay the Linux-side comparison on the table: L02's 512 MiB benchmark ran on two machines — the desktop's read won by about 15%, the laptop's mmap won by about a quarter, with the observable explanation that the two machines' per-fault cost differs by about five times. The Windows machine here gives a third answer: the warm sequential mapping wins big. Three answers side by side teach the same lesson — the winner follows the per-fault cost and the cache regime. Which answer your own machine holds is a matter of measuring with e4, a few minutes' work. So when do you use a mapping? Random access, repeated access, cross-process sharing, wanting zero copy — those are its home turf. For a one-pass sequential mega-scan, for wanting cross-platform-consistent behavior, or for not wanting to tend to exceptions, switching to `ReadFile` is the easier road.

## The File Truncated Behind Our Back? Windows Simply Refuses the Cut

The most chilling experiment in the Linux chapter was SIGBUS: the mapping sits there in fine health, the file gets halved behind its back by `ftruncate`, and the moment we touch the out-of-range region the process is carried off by the signal on the spot. This time we move the same script over to Windows: prepare a 128 KiB file and a `PAGE_READWRITE` mapping, then have a second `GENERIC_WRITE` handle walk `SetFilePointerEx` out to 64 KiB and execute `SetEndOfFile` (excerpt of `demo4`):

```cpp
SetLastError(0);
HANDLE bad = MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 4096, 0);  // 页对齐还不够
std::printf("(1) offset 4096 : base=%p err=%lu\n", bad, GetLastError());

try {
    check_win32("SetEndOfFile", SetEndOfFile, cutter.get());
} catch (const std::system_error& e) {
    std::printf("(2) SetEndOfFile refused: err=%d\n", e.code().value());
}

// 16 字节文件的读写视图:页尾(EOF 之后、页之内)语义
mapped_view tail_view{tail_mapping.get(), FILE_MAP_WRITE};
std::printf("(3) tail view at %p, bytes 16..19 = %d %d %d %d\n", static_cast<void*>(tail_view.get()),
            tail_view.get()[16], tail_view.get()[17], tail_view.get()[18], tail_view.get()[19]);
tail_view.get()[20] = 'X';  // (4) 写页尾:居然不炸
check_win32("FlushViewOfFile", FlushViewOfFile, tail_view.get(), 0);
// (5) 再开句柄读:文件还是 16 字节,内容原样,详见下方输出
```

```text
$ ./demo4.exe
(1) offset 4096 : base=0000000000000000 err=1132
(2) SetEndOfFile refused: err=1224
    file size still 131072 bytes
(3) tail view at 00000212fa3c0000, bytes 16..19 = 0 0 0 0
(4) write tail[20]='X' ... ok, no exception
(5) after flush: file is still 16 bytes, content AAAABBBBCCCCDDDD
```

The answer we want sits in row (2): **the cut does not happen**. The 1224 we received is `ERROR_USER_MAPPED_FILE`, and `SetEndOfFile`'s documentation states the requirement without an ounce of ambiguity: `"If CreateFileMapping is called to create a file mapping object for hFile, UnmapViewOfFile must be called first to unmap all views and call CloseHandle to close the file mapping object before you can call SetEndOfFile."` POSIX lets us cut, and after the cut, touching the out-of-range memory carries the process off with SIGBUS. Windows instead blocks at the file-system layer: to shorten the file we must unmap every view and close the mapping object. Row (1) additionally verified the offset requirement: 4096 is page-aligned and was still refused — the err 1132 we received is `ERROR_MAPPED_ALIGNMENT`. What it wants is in fact 64 KiB granularity.

Rows (3)~(5) complete the page-tail semantics: for a view of a 16-byte file, the stretch beyond EOF yet within the page reads back zeros. Writing into it raises no error either. We then flushed with `FlushViewOfFile` and watched the file remain 16 bytes with its content untouched — what was written never reaches the file. This is exactly the same page-tail semantics as on the Linux side: the portion of the file's end that does not fill a page reads as zeros, and writes to it never enter the file.

We met the phrase access violation already in the access table earlier, and e1's forced write gave us a taste, and now we truly put it on film. Over on Windows, it belongs to the family of structured exceptions. Since the truncation script gets blocked at the file-system layer, are structured exceptions then never needed? Not so. The documentation says it outright: `"To guard against EXCEPTION_IN_PAGE_ERROR exceptions, use structured exception handling to protect any code that writes to or reads from a memory mapped view of a file other than the page file"`. And in those moments when the pager cannot bring the data back — a network disconnect, a full disk, a device failure — the exceptions come all the same. The most common is writing to a read-only view, which the documentation's access table states in black and white: `"An attempt to write to the file view results in an access violation"`. Let us catch one, measured. One more remark I owe you: our GCC does not recognize the `__try/__except` language extension (only MSVC and Clang support it), so we instead hang a vectored handler with `AddVectoredExceptionHandler`, log the record and let it pass, letting the process terminate with its own exception code. The full mechanics of the dispatch chain get their own dedicated walk in the next chapter (demo2's sequel, with the hex-printing helper omitted. `in_watch` decides whether the faulting address falls inside the view segment we registered to watch, and that range and its spelling are likewise omitted):

```cpp
static LONG WINAPI watch_handler(EXCEPTION_POINTERS* epi)
{
    const EXCEPTION_RECORD* r = epi->ExceptionRecord;
    char* addr = reinterpret_cast<char*>(r->ExceptionInformation[1]);
    if ((r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
         || r->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
        && in_watch(addr)) {
        report_line(r);  // 异常上下文里别碰 printf:低层 WriteFile 直写控制台,思路同信号安全
    }
    return EXCEPTION_CONTINUE_SEARCH;  // 放行:无人处理,进程以该异常码退出
}
...
SetErrorMode(SEM_NOGPFAULTERRORBOX);   // 别弹"程序已停止工作"对话框
AddVectoredExceptionHandler(1, watch_handler);
std::memcpy(ro_base, "XXXX", 4);       // 往 FILE_MAP_READ 视图写:异常在此爆发
```

```text
$ ./demo2.exe; echo "exit code: $?"
(前三行同上,略)
now writing into the FILE_MAP_READ view ...
[handler] exception 0xC0000005, faulting address = 0x20EB98F0000
Segmentation fault
exit code: 139
```

The `faulting address` printed is exactly the view's base address, and `0xC0000005` is the code of `EXCEPTION_ACCESS_VIOLATION`. An unhandled structured exception takes the exception code itself as the process exit code: in bash we saw Segmentation fault with exit code 139. Switch to cmd and read %ERRORLEVEL%, and the -1073741819 you see is `0xC0000005` itself. This is the Windows cousin of SIGBUS's default kill-the-process handling. What else is written in the exception record? Who ranks earlier on the dispatch chain? Can the instruction replay after the scene is repaired? The full account of these mechanics waits for us in [Structured Exceptions: SEH and VEH](03-seh-veh.md).

## How the Other Side Sees It

Lining the two sides up: fd became HANDLE. `prot` split into the two layers of flProtect and view access. `MAP_SHARED` and `MAP_PRIVATE` correspond to `FILE_MAP_WRITE` and `FILE_MAP_COPY`. And `msync(MS_SYNC)` has to be assembled from `FlushViewOfFile` plus `FlushFileBuffers` — a lone `FlushViewOfFile` only initiates the dirty-page write-back and does not wait for the data to reach the disk, sitting between MS_ASYNC and MS_SYNC. There are asymmetries too, so let us take them one by one. The baseline for offset alignment jumps from the 4 KiB page to the 64 KiB allocation granularity — the spot where friends porting code from mmap most easily stumble. The step arrangement also differs: Linux's single mmap step becomes two on Windows, and the trade buys the freedom of closing the mapping handle early while the view stays alive (both sides in fact allow closing the file handle once the mapping is established). The criterion for changing protection also split: Linux hangs it on the fd's open mode, Windows on the view grant of MapViewOfFile — e1's measured err=87 is what the boundary of grant looks like. Lazy allocation likewise parted ways: Linux hides it in the overcommit default, Windows makes it an explicit two-phase SEC_RESERVE plus MEM_COMMIT, and the hint-passing channel changed too — the work of `madvise`'s family is taken over by the Prefetch and Offer group. The two sides diverge farthest in how things go wrong: POSIX allows the file to be cut out from under the mapping and settles the score afterward with SIGBUS. Windows' interception happens at `SetEndOfFile`, the error it gives is `ERROR_USER_MAPPED_FILE`, blocking the accident before it happens, while access violations and in-page errors are reserved for permission overreach and I/O failures. The other side's full story lives in [mmap Memory Mapping: Attaching a File to the Address Space](../../linux/file-io/02-mmap-memory-mapping.md).

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="CreateFileMappingW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw"
  />
  <ReferenceItem
    :id="2"
    title="MapViewOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile"
  />
  <ReferenceItem
    :id="3"
    title="FlushViewOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile"
  />
  <ReferenceItem
    :id="4"
    title="SetEndOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile"
  />
  <ReferenceItem
    :id="5"
    title="UnmapViewOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-unmapviewoffile"
  />
  <ReferenceItem
    :id="6"
    title="VirtualProtect function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect"
  />
  <ReferenceItem
    :id="7"
    title="PrefetchVirtualMemory function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-prefetchvirtualmemory"
  />
  <ReferenceItem
    :id="8"
    title="OfferVirtualMemory function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-offervirtualmemory"
  />
  <ReferenceItem
    :id="9"
    title="AdjustTokenPrivileges function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges"
  />
  <ReferenceItem
    :id="10"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
</ReferenceCard>
