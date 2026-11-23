---
title: "Win32 File I/O: Handles, CreateFileW, and Synchronous Reads and Writes"
description: "The first article of the Windows-side systems programming track: making plain the philosophical difference between HANDLE and fd, and the trade-offs of every CreateFileW parameter — dwShareMode, absent from POSIX, deserves special attention, with the five-row sharing matrix, the two-directional sharing checks, and the deletion semantics of FILE_SHARE_DELETE measured all the way down. The shared utilities unique_handle and last_error_code carry over from the two cornerstones articles; this article defines only check_win32. Measured: error texts, handle-leak counts, the three SetFilePointerEx bases and the zero-filled hole past EOF (NTFS versus ext4's differing disk usage for holes), three-tier FlushFileBuffers timing with FILE_FLAG_WRITE_THROUGH collapsing 26-fold on small blocks, and finally a hand-rolled file copier that beats std::filesystem::copy_file four times over with a warm cache"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 32
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "页缓存与持久性:write() 返回之后发生了什么"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/file-io/01-win32-file-io.md
  source_hash: d77b1ece08838af1f3a4f13d5041ff7416732ebb4a133546c20e306c06e04f82
  translated_at: '2026-11-22T15:43:47+00:00'
  engine: glm-5.3
  token_count: 28989
---

# Win32 File I/O: Handles, CreateFileW, and Synchronous Reads and Writes

From this article onward, we drive the systems programming volume onto the Windows side. The Win32 API is a complete user-mode C interface: window messaging and its kin live in user32.dll, while infrastructure like files, processes, and threads lives in kernel32.dll. After Win7, most of kernel32's exports kept only a thin-forwarding identity — the real implementations moved into kernelbase.dll. One level down, it is ntdll.dll's native API's turn on stage: the syscalls into the kernel are its stock-in-trade, and the file object's name in the native API is NtCreateFile. When you personally call CreateFileW, the route passes kernel32, kernelbase, ntdll, and only then truly enters the kernel. The C++ side has it easy: one `#include <windows.h>` puts every declaration in hand. We also take the chance to define the two macros `WIN32_LEAN_AND_MEAN` and `NOMINMAX`, and those several hundred unrelated headers, together with the `min`/`max` macro pair of troublesome old acquaintances, are all shut outside the door.

The W suffix on file name tails is another historical knot we cannot dodge. The `CreateFile` we usually see is actually a macro: with `UNICODE` defined it maps to `CreateFileW`, whose character set is UTF-16; without it, it maps to `CreateFileA`, which goes through the ANSI code page — on a Chinese system, GBK. Take the A version to open a path the current code page cannot express and it crashes on the spot, so modern code uniformly names the W version with `wchar_t`, and this series is no exception. As for converting between `char`, `char8_t`, and wide characters, the most worry-free way is to hand it to [std::filesystem::path](https://en.cppreference.com/w/cpp/filesystem/path); this article does not expand on it.

Environment and numbering conventions also go up front, since every number later checks against them. The machine is still the author's Win11 (build 26200), the compiler is MSYS2 UCRT64 g++ 16.1.0, and the data lands on the NTFS system disk (NVMe, WD_BLACK SN7100). Compilation and runs launch Windows programs from WSL across systems; the interop chain has three practical essentials: the working directory must stay on WSL's filesystem — artifacts land without the execute bit and need one `chmod +x` — while the experiments' target files always sit on the real NTFS under `%TEMP%`, bypassing the `\wsl.localhost` 9P path (9P is the network protocol WSL uses to expose its filesystem to Windows programs), or we would be measuring a network filesystem. The error-path demonstration is demo1_error.cpp, demo1 for short below; it is the only demonstration in this article printed in full. The read-full counting, the handle-count comparison, the SetStdHandle redirection, and the closing copier benchmark were early experiments from back in the day whose code was not archived — what remains in the body is the key code lines and the measured outputs. Reproducible, with raw outputs, are the makeup-segment e-series experiments, numbered e1 through e3b, with code and raw outputs under `code/volumn_codes/vol8/systems-programming/windows/file-io/01-win32-file-io-supplement/` in the repository, in the three directories 01-pointer-size, 02-flush, 03-sharemode; reproduction commands are in each directory's README. The two thinking articles and the later file-mapping and SEH (structured exception handling) articles also use lowercase e; each article's numbering answers only to its own archive — go by file names and you will not mistake anyone.

The division of labor among the shared utilities also deserves your attention. `unique_handle` is defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md); `last_error_code` is defined in [The Error-Handling Paradigm](../../thinking/02-error-paradigm.md) — those two cornerstones articles are their one and only definition site, and this article and the later Windows articles only reference them, never redefine them. What stays defined in this article is only `check_win32`: a Windows-side assertion helper wrapping sentinel-value checking, governing the failure branches of our Win32 API calls, ready for the later articles to reuse.

## Handles: Win32's Everything Is an Object

The POSIX side has the saying we are used to hearing — everything is a file: an fd is a small integer index into the process's file table, with 0, 1, and 2 taken by the standard streams at birth. The Win32 side goes by another saying — everything is an object, each with its own handle: files, processes, threads, events, and mutexes are all objects at the kernel level; a file gets its handle from CreateFileW, a process from OpenProcess, an event from CreateEventW, and each sends back a `HANDLE`. It is in essence an opaque `void*` value, as wide as a pointer, and one function, CloseHandle, covers the handles of every kernel object. Look at line (1) of demo1 below: the failure value you can witness with your own eyes is `ffffffffffffffff`, which is exactly the `(HANDLE)-1` we speak of.

::: warning Failure values: both INVALID_HANDLE_VALUE and NULL exist
We must lay the families' failure values side by side: Win32 APIs' failure-value conventions are not unified, and checking the wrong way earns us silent bugs:

- CreateFileW fails by returning **INVALID_HANDLE_VALUE** (-1) — do not check against NULL
- Most kernel-object creation functions like CreateFileMappingW and CreateEventW fail by returning **NULL** — we must check the other way
- GetStdHandle returns INVALID_HANDLE_VALUE on error and NULL when the process has no associated handle — both occur, so we must guard both
- BOOL-returning APIs like ReadFile/WriteFile/CloseHandle fail with FALSE; the details come from GetLastError

Before writing error-checking code, please read the documentation's Return value section — do not go from memory.
:::

## CreateFileW: Parameter by Parameter

`CreateFileW(L"demo.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)` gives us seven parameters, and the five sandwiched in the middle are today's worthwhile walk-through: the first, lpFileName, is the path, already in use in the example code, and the seventh, hTemplateFile, is unused in this article — passing nullptr is fine. **dwDesiredAccess** states what access we want: `GENERIC_READ` and `GENERIC_WRITE` are macro-level generalizations, and more specific access bits can be combined underneath. **dwShareMode** answers whether others can still open this file: giving `0` means exclusive; what we are willing to allow, we grant as combinations of `FILE_SHARE_READ`, `FILE_SHARE_WRITE`, and `FILE_SHARE_DELETE`. The part demanding full attention is this: a dimension POSIX entirely lacks — Linux has no such open mechanism at all. Line (2) of demo1 shows a collision right away, and the complete sharing matrix waits for dwShareMode's dedicated section later in this article.

**dwCreationDisposition** is a mutually exclusive set of values; their correspondence with POSIX flags is listed below:

| Value | Semantics | POSIX approximation |
| --- | --- | --- |
| CREATE_NEW | Create if absent; fail if present (ERROR_FILE_EXISTS) | O_CREAT\|O_EXCL |
| CREATE_ALWAYS | Truncate if present; create if absent | O_CREAT\|O_TRUNC |
| OPEN_EXISTING | Must exist, else ERROR_FILE_NOT_FOUND | Without O_CREAT |
| OPEN_ALWAYS | Open if present; create if absent | O_CREAT |
| TRUNCATE_EXISTING | Must exist and be emptied (needs GENERIC_WRITE) | O_TRUNC (without O_CREAT) |

**lpSecurityAttributes** we pass as `nullptr` throughout this article, seeking the default security descriptor plus non-inheritable handle combination. The one member of that struct in real use is bInheritHandle, which we save for the file-locking article — that one will test locks with inherited handles and expand the details there. The tail of **dwFlagsAndAttributes** can stack `FILE_FLAG_*` and `FILE_ATTRIBUTE_*`: two acquaintances to make today. `FILE_FLAG_OVERLAPPED` is the entrance to asynchronous I/O; a later article in this series will give it a proper stage, so here we learn its face. The other, `FILE_FLAG_WRITE_THROUGH`, is the write-through switch, corresponding to POSIX's `O_SYNC`; the good show it stacks up waits for the FlushFileBuffers section. Next article's file mapping (02) also departs from today's handle.

## Error Handling: check_win32, and Two Borrowed Shared Utilities

GetLastError keeps one slot per thread, and the official documentation says it plainly — verbatim after a failure, `call GetLastError immediately`: any Win32 call that follows, even a succeeding one, may overwrite that slot. So the first line of a failure branch should box it into `std::error_code`, and the boxing function `last_error_code` was already defined and verified on the spot in [The Error-Handling Paradigm](../../thinking/02-error-paradigm.md); we simply borrow it here. The checking work goes to this article's `check_win32`, collected together with the two shared utilities borrowed from the cornerstones into `win_util.hpp`:

```cpp
// win_util.hpp —— Windows 侧工具箱:check_win32 的定义处(本系列唯一定义)
// last_error_code 与 unique_handle 的定义在思维基石两篇,两件都收进本头:
// last_error_code 的全文印在下面,unique_handle 的类体见 RAII 范式那篇,正文不重印
#pragma once

#define WIN32_LEAN_AND_MEAN  // 本头必须最先被 include,否则这两个宏可能已被预定义
#define NOMINMAX

#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <windows.h>

// GetLastError 立即装箱:Win32 错误码挂 system_category(定义见思维基石·错误处理范式)
inline std::error_code last_error_code() noexcept
{
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}

// Win32 调用包装:失败值语义按 API 而定——句柄类 NULL / INVALID_HANDLE_VALUE,
// BOOL 类 FALSE,命中即抛 std::system_error{last_error_code(), what}
template <class F, class... Args>
auto check_win32(const char* what, F&& f, Args&&... args)
{
    auto result = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    bool failed = false;
    if constexpr (std::is_pointer_v<decltype(result)>) {
        failed = result == nullptr || result == INVALID_HANDLE_VALUE;
    } else {
        failed = result == 0;  // BOOL 类 API 失败返回 FALSE
    }
    if (failed) {
        throw std::system_error{last_error_code(), what};
    }
    return result;
}

// unique_handle(HANDLE 的 RAII,move-only):类体在思维基石·RAII 范式,本头原样收进,正文不重印
```

check_win32's difficulty lies in the non-uniform failure values, which the warning above already showed us one round of. The template splits the flow with `if constexpr`: for pointer and HANDLE returns, failure wears the faces of NULL and INVALID_HANDLE_VALUE; for BOOL returns, the failure value is 0. The trouble of non-uniform failure values is thereby handed to the type system to claim. Next we run the entire error path; the dwShareMode exclusive collision is also arranged here:

```cpp
// demo1_error.cpp —— 失败值、ERROR_SHARING_VIOLATION、两个 category 的两套宇宙
#include "win_util.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

std::string win32_text(DWORD e)  // FormatMessageW 拿系统文本,转 UTF-8 再打印
{
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring ws = buf ? buf : L"(no message)";
    LocalFree(buf);
    while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n')) { ws.pop_back(); }
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr,
                                nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}

int main()
{
    fs::path dir = fs::temp_directory_path() / "sysprog-win01";
    fs::create_directories(dir);
    auto open_read = [&](const fs::path& p) {
        return CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    };

    // (1) 不存在的文件:失败值是 INVALID_HANDLE_VALUE;错误码必须立刻取
    HANDLE h = open_read(dir / "no_such_file.bin");
    DWORD e = GetLastError();
    printf("(1) h=%p err=%lu text=%s\n", h, e, win32_text(e).c_str());

    // (2) 独占语义:dwShareMode=0 挂着,第二个 CreateFileW 就撞 ERROR_SHARING_VIOLATION
    unique_handle keeper{check_win32("CreateFileW", CreateFileW, (dir / "locked.bin").c_str(),
                                     GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr)};
    (void)open_read(dir / "locked.bin");
    DWORD e2 = GetLastError();
    printf("(2) err=%lu text=%s\n", e2, win32_text(e2).c_str());

    // (3) 同一个数字 32,两个 category 两套宇宙
    printf("(3) system(32)=%s\n    generic(32)=%s\n", std::system_category().message(32).c_str(),
           std::generic_category().message(32).c_str());
}
```

We compile and run under MSYS2 UCRT64 g++ 16.1.0 with `g++ -std=c++23 -Wall demo1_error.cpp -o demo1.exe && ./demo1.exe`; if the DLL dependencies annoy you, adding `-static` settles it. The code is all standard Win32 plus standard C++ and compiles under MSVC just the same:

```text
(1) h=ffffffffffffffff err=2 text=系统找不到指定的文件。
(2) err=32 text=另一个程序正在使用此文件，进程无法访问。
(3) system(32)=??һ???????????????ʹ??ļ?????????????ʷ???
    generic(32)=Broken pipe
```

The output's three lines, one by one. Line (1)'s `ffffffffffffffff` is INVALID_HANDLE_VALUE's hexadecimal face; error code 2 corresponds to ERROR_FILE_NOT_FOUND, and FormatMessageW thoughtfully provides the Chinese system's human-readable text. Line (2)'s output is the measured exclusive semantics of dwShareMode: keeper holds the file with sharing mode 0, and the second handle, asking for nothing but GENERIC_READ, is refused at the door — error code 32, ERROR_SHARING_VIOLATION. Treatment like this, we never enjoyed on Linux.

The most interesting is line (3). Look at the same 32: filed under `system_category`, it yields Win32's error text; filed under `generic_category`, it becomes errno 32's (EPIPE) Broken pipe. Two universes of error, distinguished by category. As for that line of mojibake, we trace its origin byte by byte: libstdc++'s `system_category().message()` on Windows emits ANSI code page (GBK) bytes, the author's UTF-8 terminal accepts them wholesale, and that is the shape you get. So for human-readable text, do not lean on message(); honestly use FormatMessageW with CP_UTF8 — that is the proper road.

## unique_handle: Borrowed and Used, Handle Leaks Counted on the Spot

We already built `unique_handle` line by line in [The RAII Paradigm](../../thinking/01-raii-paradigm.md): fd swapped for HANDLE, close swapped for CloseHandle, the null value -1 swapped for INVALID_HANDLE_VALUE — the rest needed not one word changed; both camps share the same C++ soul. That article also measured a trap: functions like CreateEventW fail by returning NULL, and stuffing that straight into a unique_handle that only recognizes the -1 sentinel gets it misjudged as valid. The checking must happen before the step of loading into RAII — precisely the reason check_win32 exists.

Handle leaks are stealthier than fd leaks: a single process's quota is roomy and will not blow up for a while; what leaks is the kernel object and the memory it holds, seeping out bit by bit. GetProcessHandleCount reads out the process's current handle count — the handle column in Task Manager reads it too. Let us try a run: in the first round we raw-open 5000, read the count, then manually CloseHandle them one by one; in the second round we switch to RAII and open 5000 the same way as the control. The program skeleton matches demo1's; the counting work sits in `hc()`, and one line — `check_win32("GetProcessHandleCount", GetProcessHandleCount, GetCurrentProcess(), &n)` — suffices; what remains is two loops:

```text
baseline                : 95
5000 raw opens, no close: 5095
raw handles all closed  : 95
RAII block alive        : 5095
RAII block exited       : 95
```

The 5000 raw opens pushed the count to 5095; close them all and 95 comes right back. The RAII group likewise pushed the count to 5095, and the moment the scope exits, unique_handle's destructor does the CloseHandle automatically, returning to 95 at full health in place. Leak prevention cannot rely on our discipline; what it can rely on is only types.

## ReadFile/WriteFile: Fill the Request, or Hit EOF

On a synchronous handle (no FILE_FLAG_OVERLAPPED, lpOverlapped passed as nullptr), ReadFile's return condition, in the document's words, is `The ReadFile function returns when one of the following conditions occur: The number of bytes requested is read...`: it blocks until **it has read the full number of bytes we requested**. On stepping to EOF the return value is TRUE with `*lpNumberOfBytesRead` giving 0; the last packet of data straddling EOF returns the remaining bytes. The difference from POSIX read here is essential — Linux's man 2 read (man7.org) says verbatim `It is not an error if this number is smaller than the number of bytes requested`: pipes, terminals, and signal interruptions all cause short reads, so the POSIX side's loop must handle read-but-not-full. On Win32's synchronous file handles, we need none of that. In the measured code we take a 4096-byte request against a 100000-byte file: 24 reads all full 4096, the 25th returning the remaining 1696, the 26th returning TRUE with got=0. When the loop exits, only one condition remains — got==0:

```text
file=100000 request=4096: reads=25 full=24 partial=1(last=1696) total=100000
```

WriteFile works the same way; `lpNumberOfBytesWritten`'s address must likewise be given. The document's one exception is pipes again: on a non-blocking byte-mode pipe, when the buffer is insufficient, WriteFile still returns TRUE, and `*lpNumberOfBytesWritten` comes in below `nNumberOfBytesToWrite`. So short writes on pipes are normal; on ordinary disk files it does not apply — knowing the exception exists is enough.

## Pointers and Sizes: SetFilePointerEx and GetFileSizeEx

Reads and writes both follow the current pointer; Win32's answer for moving the pointer and asking the size is a pair of Ex-suffixed functions, whose POSIX counterparts are lseek plus fstat. Following makeup experiment e1's output, we see the behavior's boundaries all at once (from `e1_pointer_moves.cpp`; our file is 10 bytes, content ABCDEFGHIJ). The archive's segment [2] covers the idiom for querying the current position, which we give directly in the body below and do not re-paste in the block:

```text
[1] 三种基准 + 正负偏移(文件 10 字节,内容 ABCDEFGHIJ)
  FILE_BEGIN  +3                     -> pos=3 err=0
  FILE_CURRENT +2                    -> pos=5 err=0
  FILE_CURRENT -1                    -> pos=4 err=0
  FILE_END  -3                       -> pos=7 err=0
[3] 越过 EOF 移动指针:合法,尺寸纹丝不动
  FILE_BEGIN +103 (越过 EOF)       -> pos=103 err=0
  移动后 GetFileSizeEx                  -> 10  (仍是 10)
[4] 在越过 EOF 的位置读:TRUE + 0 字节,不是错误
  ReadFile(16B)                        -> ret=1 读到=0 err=0
[5] FILE_BEGIN 配负偏移:失败,ERROR_NEGATIVE_SEEK
  FILE_BEGIN -100                    -> 失败 ret=0 err=131(ERROR_NEGATIVE_SEEK)
  此后指针停在原地                     -> pos=103
```

The three bases' usage (FILE_BEGIN/FILE_CURRENT/FILE_END with positive and negative offsets) is completely isomorphic to lseek's SEEK_SET/SEEK_CUR/SEEK_END — one glance and we pass. What deserves a close look is the last three segments. Moving the pointer past EOF is legal; GetFileSizeEx's reported size does not budge, and a ReadFile from that position gives TRUE plus 0 bytes — EOF's treatment here is not an error, interlocking exactly with the previous section's convention. The only true failure is FILE_BEGIN with a negative offset: error code 131 corresponds to ERROR_NEGATIVE_SEEK, the pointer stays where it was, and it never slides halfway to some other position. The idiom for querying the current position is also supplied here: take the combination of distance 0 with FILE_CURRENT — query only, move nothing.

### Writing Past EOF: Holes, and the Two Families' Disk-Usage Difference

With the pointer past EOF, writing onward makes the skipped middle span a **hole** — reading it yields all zeros. e1b's measurement: write 2 bytes AB at the head, jump the pointer to 1 MiB and write 1 byte Z; GetFileSizeEx reports 1048577, and the first 16 bytes read back are `41 42 00 00 ...` — all zeros after AB, with `5A` (Z's hexadecimal) meeting us at the hole's tail. This half of the behavior agrees between the two filesystems; the true fork is in disk usage. We lay the three readings together:

| Reading | NTFS default (non-sparse) | NTFS after FSCTL_SET_SPARSE (the sparse switch sent down via DeviceIoControl) | ext4 (e1e, WSL-side comparison) |
| --- | --- | --- | --- |
| Logical size | EndOfFile=1048577 | 1048577 | st_size=1048577 |
| Space actually occupied | AllocationSize=1052672 | 131072 | st_blocks=16 (8 KiB equivalent) |

The same 1 MiB hole plus solid writes at both ends: NTFS in the default state books the whole span into AllocationSize — the hole occupies disk all the same. Send one `FSCTL_SET_SPARSE` via `DeviceIoControl` to turn the sparse switch on, and the same hole's AllocationSize drops to 131072, counting only the two solid-written spans. ext4 grants the third column's treatment by birth: e1e runs in WSL on the same machine, st_blocks reads 16, and 8 KiB holds a 1-MiB-plus-1 file. The two AllocationSize numbers (1052672 and 131072) are observations of that round's NTFS allocation strategy on this machine; on your re-run the order of magnitude should agree while the values may differ. One sentence gathers it: holes read as zeros everywhere, and as for saving space — ext4 saves by birth, while on NTFS you must explicitly flip the sparse switch. When we port code across camps, disk-usage numbers are the easiest place to be fooled.

> e1b also yielded one finding inconsistent with the document: writing past EOF on a `FILE_FLAG_NO_BUFFERING` handle — the official File Buffering document says zero-fill relies on the cache manager and unbuffered writes may eat ERROR_INVALID_PARAMETER (87), while this machine measured ret=1, the write went in, and the hole read back zeros. We record it as-is; do not gamble on that error path by the old document either.

### Size Hangs on the File; the Pointer Hangs on the File Object

Windows offers more than one road to asking the size. e1c asked four ways in one breath: GetFileSizeEx, GetFileInformationByHandle's 64-bit assembly, GetFileInformationByHandleEx's `FileStandardInfo.EndOfFile`, plus the elder 32-bit GetFileSize. We cross-checked the readings — all four agree; the archive's segment [1] header counted only the three modern readings, with the old 32-bit version our addition. More interesting are two handles on the same file: handle B writes 1 byte at 1 MiB, and when we re-read in place with handle A, all four readings sweep up to 1048577 in unison. The size hangs on the file — every open handle sees it. BY_HANDLE_FILE_INFORMATION also hands over identity fields: two CreateFileW calls yield the same nFileIndex (002300000001B2AD; this value differs per machine — the point is the two match), proving they opened the very same file. We paste e1c's output for a side-by-side read:

```text
[1] 句柄 A 的三路尺寸读数
    GetFileSizeEx                       -> 100
    GetFileInformationByHandle 64 位拼装 -> 100
    FileStandardInfo.EndOfFile          -> 100
    GetFileSize(老 32 位版)             -> 100   (与上面一致)
[2] 句柄 B 在 1 MiB 处写 1 字节后,句柄 A 重新读
    GetFileSizeEx                       -> 1048577
    BY_HANDLE_FILE_INFORMATION 64 位拼装 -> 1048577
    FileStandardInfo.EndOfFile          -> 1048577
    FileStandardInfo.AllocationSize     -> 1052672  (分配尺寸,含洞策略)
    —— 尺寸挂在文件上,A 不动也看得见
```

Look at segment [2]'s four roads: handle B wrote but one byte, handle A never moved, and the readings all rose together to 1048577, while AllocationSize still exposes the hole's disk-usage policy.

And the pointer's behavior? e1d and e1e ran the same comparison pair on the two systems. We open one handle per CreateFileW call; after h1 reads 4 bytes, h2's pointer has not budged, and reading from 0 still yields the same 4 bytes. But h3, copied out by DuplicateHandle, inherits h1's position at birth: h3 reads 4 bytes and h1 gets pushed along; h3 backs up and h1 backs up too. The archive's DuplicateHandle segment, carried over verbatim:

```text
[3] DuplicateHandle(h1 -> h3):同一个文件对象,指针共享
    h3=00000000000000f4 诞生即继承 pos(h3)=6  (h1 刚读完,停在这)
    h3 读到 "6789"
    读后  pos(h3)=10  pos(h1)=10   <- h1 被 h3 的读推着走
          pos(h2)=4                <- h2 纹丝不动
    h3 回退 -4 后 pos(h1)=6  pos(h3)=6  <- h1 跟着回退,共享实锤
```

Watch h3: it landed standing right at 6, where h1 had just stopped; the 4 bytes it read pushed h1 to 10, while h2's 4 was never touched from head to tail. e1e also ran the same comparison in WSL on the same machine:

```text
$ ./e1e_linux_side
[1] open x2:fd1=3 fd2=4
    fd1 读 4 字节:"0123"  -> fd1 偏移=4 fd2 偏移=0
[2] dup(fd1)=fd3 读 4 字节:"4567" -> fd1 偏移=8 fd3 偏移=8  <- 互相推进
```

Reading them side by side: two fds from open hold independent offsets; an fd from dup shares its offset — agreeing line by line with the Windows side's e1d. We also set up the mechanical correspondence: Windows's file object maps to POSIX's open file description — each `CreateFileW` call builds a new one, and what DuplicateHandle and dup copy is merely the table entry pointing to it. So the claim that every CreateFileW shares pointers is disproven by measurement on both sides; only the two lineages of copying and inheriting can share.

## FlushFileBuffers: After WriteFile Returns, Is the Data on Disk?

WriteFile returning happily only means the data entered the system cache. The full story of that sentence was verified end to end on the Linux side in [Page Cache and Durability](../../linux/file-io/03-page-cache.md) with Dirty counts, kill -9, and timing experiments: write() returned, and the disk's business had not yet happened. The Windows-side mirror question is one line: who pushes the data to disk for us? The answer is FlushFileBuffers, occupying fsync's position. Makeup experiment e2 carries over L03's methodology: the same 32 MiB of data walks multiple persistence paths, writing and flushing timed separately, 5 full rounds with the median taken, and `FILE_FLAG_WRITE_THROUGH` (wt for short) corresponds to L03's O_SYNC:

```text
mode        write_ms   flush_ms   total_ms  write_MiB_s  total_MiB_s
plain          6.772      0.000      6.772         4725         4725
flush          6.831      8.012     14.774         4685         2166
wt            14.975      0.000     14.975         2137         2137
wt_flush      15.882      0.205     16.086         2015         1989
wt_4k            -          -    389.295            -           82
```

The gap between plain and flush is the gap between entering the cache and reaching the disk: the write loop alone took 6.8 ms (4725 MiB/s — the cache's speed), the one compensating FlushFileBuffers cost 8.0 ms, and total throughput fell to 2166 MiB/s — the same story as L03's plain (6337) versus fsync (4224). wt and the write-then-flush-once road arrive at the same destination by different routes: the two sides' scores are 2137 against 2166, both bound by the disk's sustained write speed, differing only in when they wait for the disk. Every wt write waits for the disk; flush banks a batch and leaves the waiting to the close. So wt with 4 KiB small blocks collapses outright: throughput falls to 82 MiB/s, one twenty-sixth of the large-block figure — the same shape as L03's O_SYNC with 4 KiB (falling to 3 MiB/s); the price of small blocks plus synchronization likewise does not care which machine. The compensating flush after wt cost only 0.205 ms — there were few dirty pages left to flush on the write-through path. And think the other way: within plain's 6.8 ms, not one bit of durability work was done.

> On a fast disk the plain-versus-flush gap is only around 2.2x; swap in a mechanical disk or a machine with the disk's write cache disabled and the gap grows much wider. The Windows side has no /proc/meminfo Dirty to watch, so we cannot build an observation window as clean as the Linux side's; we can only delete the file each round and lean on the previous round's already-flushed disk to suppress inter-round interference. The numbers belong to the author's machine; only the relative relationships transfer.

### fflush, WriteFile, FlushFileBuffers: Three Layers, Each Governing Its Own Stretch

Friends mixing in the C runtime need one more layer split out. e2b ran FILE* against a bypass Win32 handle: after byte-by-byte fwrite writes 100 bytes, the size the bypass handle sees before fflush is 0 — the data is still banked in the CRT's user-mode buffer, and not even a WriteFile call has happened. Only after fflush does the size read 100. A large 1 MiB-block fwrite goes straight through to WriteFile: the moment 32 MiB finishes, the bypass handle sees 33554432. Whether the bypass handle sees it depends on whether the data block has entered the OS, not on whether you called fflush. The three layers' timing was measured too: fwrite took 6.9 ms to send 32 MiB into the cache; the first fflush cost 5.0 ms (clearing CRT residue); the second spun empty at 0.000 ms; the 9.2 ms is what it cost to truly push the data to disk. We join e2b's three output segments into one view:

```text
[1] CRT 层持留:FILE* 逐字节写 100 个 's'
    fwrite 完成未 fflush,旁路句柄看到的尺寸 -> 0
    fflush 之后,旁路句柄看到的尺寸       -> 100  (fflush 耗时 0.066 ms)
[2] 三层计时:32MiB(1MiB 块 fwrite)
    fwrite  32MiB      ->    6.892 ms  (进 OS 缓存)
    fflush 第一次      ->    4.963 ms  (清 CRT 残余)
    fflush 第二次(空) ->    0.000 ms  (无残余时的固定开销,对照用)
    FlushFileBuffers   ->    9.202 ms  (刷缓存管理器脏页 + 存储栈)
[3] 尺寸可见性:旁路句柄在每步之后看到的字节数
    fwrite 完(未 fflush) -> 33554432  <- 大块 fwrite 已直通 OS,不在 CRT 缓冲里
    fflush 之后           -> 33554432
    FlushFileBuffers 之后 -> 33554432(尺寸早就在了,这一步买的是持久性,不是可见性)
分层:fwrite/fputc -> CRT 缓冲 -> WriteFile -> 系统缓存 -> 盘
     fflush 清 CRT 层;FlushFileBuffers 清系统缓存层;谁也不越层替别人干活
```

fclose, arriving at the close, does only an fflush in passing; nobody calls FlushFileBuffers for you — you must ask for that layer yourself. The file-mapping side has a relative, FlushViewOfFile: it only initiates the write-back of dirty pages and does not wait for the hardware; to assemble fsync's semantics, the document's named practice is to follow it with one FlushFileBuffers. FlushViewOfFile itself appears in the next article on file mapping — what that side measures is it appearing alone, and the basis for the two-step combination is the document's verbatim quote cited there. One more note: FlushFileBuffers also accepts a volume handle (what an administrator gets opening `\\.\C:` with GENERIC_WRITE), flushing the whole volume's pending writes — a system-wide effect; leave it alone for no reason. This article's experiments touched only file handles.

## Standard Handles: GetStdHandle and SetStdHandle

The Win32-level redirection practice is to fetch the current standard output's handle with GetStdHandle, while SetStdHandle does the replacing — the mirror of Linux's dup2. But there is a divergence only measurement makes clear, so we run it directly. The demonstration's actions fall into three steps: first we `printf` one line, then WriteFile one line to the console handle, and only then SetStdHandle swaps in a file handle, whose handle — from `GetStdHandle(STD_OUTPUT_HANDLE)` — we WriteFile to. Run it and you will see the content indeed entered the file, while the CRT's `printf` still went to the screen. The CRT walks its own fd 1 and never consults the Win32 standard-handle table. dup2, by contrast, edits the fd table and takes printf along:

```text
[handle] hello via WriteFile(GetStdHandle)
[printf] this still goes to the console
$ cat redirect.txt
[handle] this goes into redirect.txt
```

(The output's first line ranks first because piped stdout is fully buffered — printf banked it until exit; a little discovery picked up along the way.) When closing, do not forget to swap the original handle back with SetStdHandle. As for handle inheritance: only handles with SECURITY_ATTRIBUTES' bInheritHandle set to TRUE can be inherited by a CreateProcess child. POSIX's fork inherits everything by birth; Win32's default inherits not one — whatever we want, we explicitly enable.

## dwShareMode: The Five-Row Matrix and the Two-Directional Check

Demo1's line (2) already showed us the exclusive collision's face; now we lay out e3's whole matrix: the first handle's access fixed at GENERIC_READ|GENERIC_WRITE, the second handle's share fully granted R|W|D, and we vary only the access the second handle requests:

| First handle's share | Wants READ | Wants WRITE | Wants R\|W | Wants DELETE |
| --- | --- | --- | --- | --- |
| 0 (exclusive) | Denied 32 | Denied 32 | Denied 32 | Denied 32 |
| R | OK | Denied 32 | Denied 32 | Denied 32 |
| W | Denied 32 | OK | Denied 32 | Denied 32 |
| R\|W | OK | OK | OK | Denied 32 |
| R\|W\|D | OK | OK | OK | OK |

The matrix's denied-32 always means ERROR_SHARING_VIOLATION. And note our matrix opened the second handle inside the same process: the sharing check hangs on the kernel's file object and pays no mind to origin — our own handles get stopped too — while POSIX's open places no constraint whatsoever on re-opening within the same process. A dimension like this simply does not exist on the Linux side. e3 also re-verified across processes: the parent holds the file with share=0, a true child process connects via a two-round event handshake — round one's CreateFileW(READ) is denied 32; once the parent lets go, round two gets the handle. Behavior inside and outside the process boundary is identical.

From the matrix alone you might read half a rule; here is the other half: the check runs in both directions. The new handle's requested access must be allowed by every present handle's share, and the access the present handles are actually using must also fit under the new handle's share — both checks must pass. e3's bidirectional segment is designed exactly so: the first handle's access is R|W and its share is R|W too; the second handle requests only READ, and we give it three share settings:

```text
== [2] 双向检查:第二个句柄的 share 也要装得下第一个句柄的在用访问 ==
    (第一句柄:访问 R|W、share R|W;第二句柄只要 READ,但 share 各配一档)
    第二 share=0(独占) 第二个访问=READ       -> 拒绝 err=32(ERROR_SHARING_VIOLATION)
    第二 share=R         第二个访问=READ       -> 拒绝 err=32(ERROR_SHARING_VIOLATION)
    第二 share=R|W       第二个访问=READ       -> 开到 h=000000000000015c
```

The three lines' reading, one pass: the share=0 row is denied 32; the share=R row is denied 32 too, because it cannot hold the first handle's in-use write access; only when the share grants the full R|W does the handle open. So share declares how I allow others to touch this file, not that I can open the door — read that sentence backwards and the matrix will chafe however you read it.

### Deletion Semantics: With or Without FILE_SHARE_DELETE, Two Worlds Apart

The matrix's last column hides this article's most divergent piece: DELETE access. Without D in the share, DeleteFileW and MoveFileExW both meet refusal, the measured error code 32 (SHARING_VIOLATION). Quite a few references write 5 (ACCESS_DENIED) here; at least on the author's Win11 26200 it is not — we stand by the measurement. Once the handle closes, the very same MoveFileExW succeeds at once: what blocked it was the handle itself.

After the share grants the full R|W|D, the picture changes worlds; we carry e3's output over verbatim:

```text
(b) share=R|W|D 句柄在握:
    DeleteFileW -> ret=1 err=0
    DeletePending -> 1  (FileStandardInfo 直读)
    ReadFile -> ret=1 读到=9 前8字节=44454C4554452D4D
    WriteFile -> ret=1 写到=9  (delete pending 下句柄照常干活)
    GetFileAttributesW -> 0xFFFFFFFF err=2
    新开句柄(OPEN_EXISTING) -> 拒绝 err=2
    最后一个句柄关闭后 GetFileAttributesW -> 0xFFFFFFFF(文件真正回收)
(c) share=R|W|D 句柄在握时改名 -> ret=1 err=0
    改名后原句柄 ReadFile -> ret=1 读到=9 "RENAME-ME"(句柄跟文件走)
```

DeleteFileW succeeded; FileStandardInfo directly reads a DeletePending flag of 1, and the handle in our hand keeps working with ReadFile/WriteFile as usual (the 9 bytes read back decode in hexadecimal to DELETE-M). And the file name? Gone on the spot: GetFileAttributesW reports err=2, a newly opened handle reports err=2 — both seeing FILE_NOT_FOUND. Only after the last handle closes is the file truly reclaimed. Behavior in the same lane as POSIX's unlink: the name is plucked immediately, and open handles keep it alive until the last close. The classic delete-pending form written in older references — the name surviving to the last close and new opens eating ACCESS_DENIED — did not appear on the author's Win11 26200. Should you catch a system upgrade, the behavior may drift; on re-runs, let the re-run .out be the authority. Renaming gets the same treatment: it succeeds, the original handle follows the file, and the new name lives on in fine health.

> e3b verified along the way that fully-open sharing does not mean everything synchronizes: we swapped the read side to a FILE_FLAG_NO_BUFFERING direct-read handle. The document warns that dirty pages from buffered writes, not yet on disk, may read stale; the first two escalations (opening a direct-read handle after writing, and holding a direct-read handle across the write) both have raw output in the .out, and everything read was fresh data. The third (writing the whole 32 MiB then reading the tail page) is noted only in the source comments and the README, never separately entering a .out, so we do not recite it as a measured convention. Not one stale byte was fished out. The negative result goes on record as-is: the document's warning stands, and if you truly must mix, we advise doing FlushFileBuffers up front.

## Practice: A File Copier, Beating copy_file Four Times Over

Having learned, we go hands-on at once and write a copier for a 512 MiB file: a pair of handles opened by CreateFileW, the ReadFile/WriteFile hauling, a source file built by make_big_file, and three timed rounds in main — all primitives from today. The core function:

```cpp
double copy_win32(const fs::path& src, const fs::path& dst)
{
    auto t0 = std::chrono::steady_clock::now();
    {
        unique_handle in{check_win32("CreateFileW", CreateFileW, src.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
        unique_handle out{check_win32("CreateFileW", CreateFileW, dst.c_str(), GENERIC_WRITE, 0,
                                      nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        std::vector<char> buf(1 << 20);  // 1 MiB 缓冲,循环里反复复用
        for (;;) {
            DWORD got = 0, put = 0;
            check_win32("ReadFile", ReadFile, in.get(), buf.data(), (DWORD)buf.size(), &got,
                        nullptr);
            if (got == 0) { break; }  // 同步句柄:got==0 即 EOF,退出循环
            check_win32("WriteFile", WriteFile, out.get(), buf.data(), got, &put, nullptr);
        }
    }  // 两个 unique_handle 在此析构,CloseHandle 计入耗时
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
```

The code's three trade-offs, one by one. The read side stacks `FILE_FLAG_SEQUENTIAL_SCAN`, telling the cache manager to do large-block read-ahead in order — the standard hint for big-file copies. The buffer is 1 MiB, reused through the loop without per-round reallocation; the two unique_handles sit in the inner scope so the CloseHandle is counted in the elapsed time, making the comparison's convention fair. The measured run on the author's machine: CPU a Ryzen 7 9700X, disk a WD_BLACK SN7100 NVMe, system Win11 26200, compiler MinGW-w64 g++ 16.1.0, cache warm, the source file just built, data still in the system cache. Three rounds' results:

```text
source: C:\Users\CHARLI~2\AppData\Local\Temp\sysprog-win01\big.bin (512 MiB)
round 1: ReadFile/WriteFile  161.7 ms | fs::copy_file  885.2 ms
round 2: ReadFile/WriteFile  156.5 ms | fs::copy_file  720.8 ms
round 3: ReadFile/WriteFile  161.4 ms | fs::copy_file  866.5 ms
```

The hand-rolled version runs steadily around 160 ms — roughly 3.2 GiB/s — while `std::filesystem::copy_file` asks 720~885 ms: a fourfold gap. Why is the standard library slower instead? The author went through libstdc++'s source: its do_copy_file on Windows walks a `_wopen` plus stdio_filebuf streaming haul. The source actually also keeps two fast paths, sendfile and copy_file_range — both Linux syscalls letting the kernel move data directly between two fds, sparing the user-mode round trips. The sendfile branch is gated by a negated macro check, `_GLIBCXX_FILESYSTEM_IS_WINDOWS` — in a Windows build that negation fails, and the branch stays out. The copy_file_range branch depends on `_GLIBCXX_USE_COPY_FILE_RANGE`, which configure simply never defines when running on MinGW. So neither the sendfile nor the copy_file_range road takes part in a Windows build — no sequential hint, plus two extra buffer layers. We cross-checked with the DLL import table: libstdc++-6.dll's imports contain no CopyFileW import at all; do_copy_file and the kernel's fast path never crossed paths as far as we could trace. If you truly want official-grade speed, Win32 has the dedicated CopyFileW to let the kernel do the work — out of this article's scope. The ReadFile/WriteFile pair you already command writes a copy four times faster than the standard library's default road.

## How the Other Side Sees It

We lay the two sides together once more. An fd is a small integer index into the process's file table; a HANDLE is an opaque pointer value. POSIX speaks everything-is-a-file: one set of open, read, write, close governs to the bottom; Win32 speaks everything-is-an-object: each object family has its own Create and Open, with the closing CloseHandle covering all. open's practice shoves everything into flags in one go; CreateFileW splits into access, share, and disposition parameter groups, buying the sharing control POSIX lacks: the five-row matrix governs who may open the door, and FILE_SHARE_DELETE's presence or absence splits the deletion semantics — one end tied to 32, the other neighboring POSIX's unlink. read permits short reads, so its loop needs a fallback; synchronous ReadFile fills the request or hits EOF, and the exit condition lands on got==0. lseek and SetFilePointerEx are isomorphic on the three bases; writes past EOF create zero-reading holes on both sides — and the hole's real occupancy: ext4 does not occupy by birth, while NTFS must explicitly turn on sparse to not occupy. fsync's work belongs to FlushFileBuffers, O_SYNC's to FILE_FLAG_WRITE_THROUGH, and the small-block-plus-synchronous collapse is the same model on both sides. dup2 edits the fd table and takes the CRT along; SetStdHandle swaps only the Win32 layer, and the CRT's printf pays it no mind. For the other side's full story, see [the POSIX File I/O article](../../linux/file-io/01-posix-file-io.md); the page cache and durability evidence chain continues in [Page Cache and Durability](../../linux/file-io/03-page-cache.md).

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="CreateFileW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew"
  />
  <ReferenceItem
    :id="2"
    title="ReadFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile"
  />
  <ReferenceItem
    :id="3"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="4"
    title="GetLastError function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror"
  />
  <ReferenceItem
    :id="5"
    title="SetFilePointerEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex"
  />
  <ReferenceItem
    :id="6"
    title="GetFileSizeEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfilesizeex"
  />
  <ReferenceItem
    :id="7"
    title="FlushFileBuffers function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers"
  />
  <ReferenceItem
    :id="8"
    title="File Buffering (FILE_FLAG_WRITE_THROUGH / NO_BUFFERING)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering"
  />
  <ReferenceItem
    :id="9"
    title="DuplicateHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle"
  />
  <ReferenceItem
    :id="10"
    title="DeleteFileW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew"
  />
  <ReferenceItem
    :id="11"
    title="Sparse Files (FSCTL_SET_SPARSE)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/fileio/sparse-files"
  />
  <ReferenceItem
    :id="12"
    title="GetProcessHandleCount function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount"
  />
  <ReferenceItem
    :id="13"
    title="GetStdHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/getstdhandle"
  />
  <ReferenceItem
    :id="14"
    author="GCC libstdc++"
    title="ops-common.h"
    publisher="GitHub"
    url="https://github.com/gcc-mirror/gcc/blob/master/libstdc++-v3/src/filesystem/ops-common.h"
  />
</ReferenceCard>
