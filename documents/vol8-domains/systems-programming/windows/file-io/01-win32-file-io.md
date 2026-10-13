---
title: "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
description: "Windows 侧系统编程第一篇:讲明白 HANDLE 与 fd 的哲学差异、CreateFileW 每个参数的取舍——POSIX 没有的 dwShareMode 尤其值得看,五行共享矩阵、双向的两道共享检查、FILE_SHARE_DELETE 的删除语义一路实测到底;公共工具 unique_handle 与 last_error_code 沿用思维基石两篇的定义,本篇只定义 check_win32;实测错误文本、句柄泄漏计数、SetFilePointerEx 三基准与越过 EOF 的零洞(NTFS 与 ext4 的洞占盘差异)、FlushFileBuffers 三层计时与 FILE_FLAG_WRITE_THROUGH 配小块塌 26 倍,最后手搓文件复制器,暖缓存下比 std::filesystem::copy_file 快四倍"
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
---

# Win32 文件 I/O:句柄、CreateFileW 与同步读写

咱们从这一篇起,把系统编程卷开进了 Windows 侧。Win32 API 是用户态的一整套 C 接口,窗口消息那一类住的是 user32.dll,文件、进程、线程这些基础设施住的则是 kernel32.dll。而 Win7 之后,kernel32 的多数导出只剩下薄转发的身份,真正的实现都搬进了 kernelbase.dll。再往下一层就轮到 ntdll.dll 的原生 API 出场,syscall 进内核的活由它包了,文件对象在原生 API 里的名字,叫的就是 NtCreateFile。您亲手调一次 CreateFileW,沿途路过的就是 kernel32、kernelbase、ntdll,最后才真正进了内核。C++ 这边就省事了,一个 `#include <windows.h>` 就把全部的声明都拿到了手。咱们再顺手把 `WIN32_LEAN_AND_MEAN` 与 `NOMINMAX` 两个宏定义好,那几百个不相干的头文件,连同 `min`/`max` 宏这对讨嫌的老熟人,就一并被挡在了门外。

文件名尾巴上的 W 后缀,又是一个绕不开的历史问题。咱们平常见到的 `CreateFile`,其实是个宏:定义了 `UNICODE`,它映射到的就是 `CreateFileW`,字符集走的是 UTF-16,而没定义的时候,映射到的则是 `CreateFileA`,走的就是 ANSI 代码页,在中文系统上跑的就是 GBK。您要是拿 A 版去开一个当前代码页表示不了的路径,它当场就翻车了,所以现代代码一律点名 W 版配 `wchar_t`,咱们这个系列也不例外。至于 `char`、`char8_t` 与宽字符之间的互转,最省心的做法是交给 [std::filesystem::path](https://en.cppreference.com/w/cpp/filesystem/path),本篇就不展开了。

环境与编号的口径,咱们也交代在开头,后面所有的数字都要靠它对表。机器用的还是笔者的 Win11(版本 26200),编译器是 MSYS2 UCRT64 的 g++ 16.1.0,数据落的盘是 NTFS 系统盘(NVMe,WD_BLACK 的 SN7100)。编译与运行咱们都从 WSL 里跨系统调起 Windows 的程序,interop 链路的实操要点有三个:工作目录得留在 WSL 的文件系统上,产物落过去是不带执行位的,咱们得 `chmod +x` 补一次,而实验的目标文件一律放 `%TEMP%` 的真实 NTFS 上,绕开 `\wsl.localhost` 的 9P 路径(9P 是 WSL 把自家文件系统亮给 Windows 程序用的网络协议),不然测的就是网络文件系统了。错误路径的演示叫 demo1_error.cpp,下文咱们简称它 demo1,它是本篇唯一把代码印全的演示。读满计数、句柄计数对照、SetStdHandle 重定向与末尾的复制器基准,这几场是当年的早期实验,代码是没有入册的,正文里留下的,是关键的代码行与实测输出。可复跑、带原始输出的,是补课段的 e 系实验,编号从 e1 排到了 e3b,代码与原始输出都收在仓库 `code/volumn_codes/vol8/systems-programming/windows/file-io/01-win32-file-io-supplement/` 下面的 01-pointer-size、02-flush、03-sharemode 三个目录里,复现命令写在各目录的 README 里。thinking 两篇与后面的文件映射、SEH(结构化异常)等篇同样用小写 e,各篇的编号只认各篇自己的存档,您认文件名就不会认错人。

公共工具的分工也请您留意一下。`unique_handle` 的定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md),`last_error_code` 的定义在[错误处理范式](../../thinking/02-error-paradigm.md),思维基石那两篇才是它们的唯一定义处,本篇与后续的 Windows 篇都只引用、不重定义。留在本篇定义的只有 `check_win32`:这个模板是 Windows 侧包着哨兵值判错的断言辅助,管的是咱们调用 Win32 API 的失败分支,后面的各篇拿去复用就好。

## 句柄:Win32 的“万物皆对象”

POSIX 那边有咱们听惯了的名言,叫的是“万物皆文件”:fd 是进程文件表里的小整数下标,0、1、2 生来就被标准流占掉了。Win32 这边走的是另一句,“万物皆对象、各拿各的句柄”:文件、进程、线程、事件、互斥体在内核的层面全是对象,文件领句柄找的是 CreateFileW,进程找的是 OpenProcess,事件找的则是 CreateEventW,各自发回的都是一个 `HANDLE`。它本质上是个不透明的 `void*` 值,宽度与指针的宽度相同,而 CloseHandle 一个函数,就通吃了所有内核对象的句柄。请您往下看 demo1 的第 (1) 行,能亲眼见到的失败值就是 `ffffffffffffffff`,它正是咱们说的 `(HANDLE)-1`。

::: warning 失败值:INVALID_HANDLE_VALUE 和 NULL 都有
咱们得把各家的失败值摆在一起看:Win32 各 API 的失败值约定不统一,判错判反了,等咱们的就是静默 bug:

- CreateFileW 失败返回 **INVALID_HANDLE_VALUE**(-1),咱们可别拿 NULL 去判
- CreateFileMappingW、CreateEventW 等多数内核对象创建函数,失败返回的又是 **NULL**,咱们得换一边判
- GetStdHandle 出错返回 INVALID_HANDLE_VALUE,而进程没有关联句柄时返回 NULL,两个都占,咱们两种都得防
- ReadFile/WriteFile/CloseHandle 这类 BOOL 返回的,失败就是 FALSE,细节咱们查 GetLastError

咱们动手写判错代码以前,请翻一遍文档的 Return value 段,别靠背了。
:::

## CreateFileW:参数逐个过

`CreateFileW(L"demo.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)` 一共给了咱们七个参数,今天值得咱们挨个过的,是夹在中间的五个:头一个 lpFileName 是路径,示例的代码里已经在用它,而第七个 hTemplateFile 本篇用不上,咱们传 nullptr 就行。**dwDesiredAccess** 说的是要什么权限:`GENERIC_READ`、`GENERIC_WRITE` 是宏层面的概括,底下还能按位组合出更具体的权限位。**dwShareMode** 回答的是“别人还能不能开这个文件”:给的是 `0`,它代表的就是独占,肯放行的,咱们就按 `FILE_SHARE_READ`、`FILE_SHARE_WRITE`、`FILE_SHARE_DELETE` 组合着给。需要咱们打起精神的地方在这里:这是 POSIX 完全没有的维度,在 Linux 上压根没有这样的 open 机制。demo1 的第 (2) 行咱们马上就能看到一次冲突,而完整的共享矩阵,咱们留到本篇后面的 dwShareMode 专节再全部过一遍。

**dwCreationDisposition** 是一组互斥的值,它和 POSIX 标志的对照,咱们列在下面:

| 值 | 语义 | POSIX 近似 |
| --- | --- | --- |
| CREATE_NEW | 不存在则建,存在则失败(ERROR_FILE_EXISTS) | O_CREAT\|O_EXCL |
| CREATE_ALWAYS | 存在则截断,不存在则建 | O_CREAT\|O_TRUNC |
| OPEN_EXISTING | 必须存在,否则 ERROR_FILE_NOT_FOUND | 不带 O_CREAT |
| OPEN_ALWAYS | 存在则开,不存在则建 | O_CREAT |
| TRUNCATE_EXISTING | 必须存在并清空(要 GENERIC_WRITE) | O_TRUNC(无 O_CREAT) |

**lpSecurityAttributes** 咱们在本篇一律传 `nullptr`,求的就是默认安全描述符加句柄不可继承的组合。结构体里真正常被用到的就是 bInheritHandle 这个成员,咱们留到讲文件锁的那一篇,那边会拿继承来的句柄试锁,到时候再展开它的细节。**dwFlagsAndAttributes** 的尾巴上还能叠 `FILE_FLAG_*` 与 `FILE_ATTRIBUTE_*`:今天咱们要认识两位,`FILE_FLAG_OVERLAPPED` 是异步 I/O 的入口,本系列的后续文章会专门请它出场,这里咱们认个脸熟就好。另一位 `FILE_FLAG_WRITE_THROUGH` 是写穿模式的开关,对应 POSIX 的 `O_SYNC`,它攒出的好戏,咱们留到讲 FlushFileBuffers 的小节再看。下一篇的文件映射(02 篇),也是从今天这个句柄出发的。

## 错误处理:check_win32,以及两件领来的公共工具

GetLastError 给每个线程留了一个槽,官方文档也明说了,失败以后的原话是 `call GetLastError immediately`:后面随便跟着的 Win32 调用,哪怕它成功了,都可能把这个槽给覆盖掉了。所以失败分支的头一行,咱们就该把它装箱进 `std::error_code`,而装箱的函数 `last_error_code`,[错误处理范式](../../thinking/02-error-paradigm.md) 那篇已经定义好并当场验过定格了,咱们这里直接领来用。判错的活儿,则交给本篇定义的 `check_win32`,连同从思维基石借来的两件公共工具,一起收在了 `win_util.hpp` 里:

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

check_win32 的难点,难在失败值的不统一,上面那个 warning 里咱们已经见过一轮了。模板里用 `if constexpr` 分流:指针、HANDLE 返回的,失败的长相就是 NULL 与 INVALID_HANDLE_VALUE,BOOL 返回的,失败的值就是 0。失败值不统一的麻烦,就这样交给了类型系统去认领。接下来咱们把错误路径全部跑一遍,dwShareMode 的独占冲突,实测咱们也安排在了这里:

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

咱们在 MSYS2 UCRT64 的 g++ 16.1.0 下编译运行,用的命令是 `g++ -std=c++23 -Wall demo1_error.cpp -o demo1.exe && ./demo1.exe`,您要是嫌 DLL 依赖烦,加上 `-static` 就可以了。代码用的全是标准 Win32 加标准 C++,它在 MSVC 底下同样是可以编的:

```text
(1) h=ffffffffffffffff err=2 text=系统找不到指定的文件。
(2) err=32 text=另一个程序正在使用此文件，进程无法访问。
(3) system(32)=??һ???????????????ʹ??ļ?????????????ʷ???
    generic(32)=Broken pipe
```

输出的三行,咱们一行一行地看过去。第 (1) 行的 `ffffffffffffffff`,就是 INVALID_HANDLE_VALUE 的十六进制长相,错误码 2 对应的是 ERROR_FILE_NOT_FOUND,FormatMessageW 还贴心地给出了中文系统的人类可读文本。第 (2) 行的输出,就是 dwShareMode 独占语义的实测:keeper 用 0 共享模式把文件挂住了,第二个句柄哪怕要的只是 GENERIC_READ,也被拒在了门外,错误码 32 对应的是 ERROR_SHARING_VIOLATION。这样的待遇,咱们在 Linux 上可从来没享受过。

最有意思的还得数第 (3) 行。咱们看同样一个 32:挂在了 `system_category` 的名下,给的就是 Win32 的错误文本,而挂在了 `generic_category` 的名下,就成了 errno 32(EPIPE)的那句 Broken pipe。两套错误的宇宙,区分它们靠的就是 category。至于输出的那行乱码,咱们顺着字节找它的来历:libstdc++ 的 `system_category().message()` 在 Windows 上吐出来的是 ANSI 代码页(GBK)的字节,笔者的 UTF-8 终端照单全收,就成了这副模样。所以想拿人类可读的文本,咱们别依赖 message(),老老实实地用 FormatMessageW 配上 CP_UTF8,走的才是正路。

## unique_handle:领来就用,句柄泄漏当场数

`unique_handle` 咱们在 [RAII 范式](../../thinking/01-raii-paradigm.md) 那篇里已经逐行造过一遍:fd 换成了 HANDLE,close 换成了 CloseHandle,空值 -1 也换成了 INVALID_HANDLE_VALUE,其余的部分一个字都不用改,两大阵营共享的是同一个 C++ 灵魂。那边还实测过一个陷阱:CreateEventW 那类函数失败返回的是 NULL,直接塞给只认 -1 哨兵的 unique_handle,它就会被误判成有效的。判错的活儿得发生在装进 RAII 的那一步之前,这正是 check_win32 存在的理由。

句柄泄漏比 fd 泄漏更隐蔽:单进程的配额宽裕,一时半会儿炸不了,漏的是内核对象和它占着的内存,一点一点地出去。GetProcessHandleCount 能把进程当前持有的句柄数读出来,任务管理器里的句柄列,读的就是它。咱们试跑一把:第一轮咱们裸开 5000 个,读一遍当前的计数,然后再逐个手动地 CloseHandle,第二轮咱们换 RAII,同样地开 5000 个做对照。程序骨架与 demo1 的相同,取数的活儿在 `hc()` 里,一行 `check_win32("GetProcessHandleCount", GetProcessHandleCount, GetCurrentProcess(), &n)` 就够了,剩下的就是两个循环:

```text
baseline                : 95
5000 raw opens, no close: 5095
raw handles all closed  : 95
RAII block alive        : 5095
RAII block exited       : 95
```

裸开的那 5000 个,把计数顶到了 5095,咱们把它们全部关掉,95 就又回来了。RAII 的那一组,同样把计数顶到了 5095,而离开作用域的那一刻,unique_handle 的析构会自动把 CloseHandle 做掉,原地满血地回到了 95。防泄漏的活儿,咱们靠自觉是防不住的,能靠的,也就只有类型了。

## ReadFile/WriteFile:读满请求,或到 EOF

同步句柄(没开 FILE_FLAG_OVERLAPPED、lpOverlapped 传的是 nullptr)上,ReadFile 的返回条件,文档的原话是 `The ReadFile function returns when one of the following conditions occur: The number of bytes requested is read...`:它会一直阻塞,直到**读满咱们请求的字节数**。而踩到 EOF 的时候,返回的值是 TRUE,`*lpNumberOfBytesRead` 给的就是 0,跨过 EOF 的最后一包数据,返回的则是剩余的字节。这一点与 POSIX read 的差异是本质性的,Linux 的 man 2 read(man7.org)原话是 `It is not an error if this number is smaller than the number of bytes requested`:管道、终端与信号打断都会造成 read 的短读,所以 POSIX 那边的循环,必须处理读了却没读满的情况。Win32 的同步文件句柄上,这些咱们都不用管。实测的代码里,咱们拿 4096 字节的请求,去读一个 100000 字节的文件:24 次全是满的 4096,第 25 次返回剩余的 1696,第 26 次返回的则是 TRUE 且 got=0。轮到循环要退场的时候,条件就只剩下 got==0 一个了:

```text
file=100000 request=4096: reads=25 full=24 partial=1(last=1696) total=100000
```

WriteFile 的道理相同,`lpNumberOfBytesWritten` 的地址同样要给到位。文档里唯一的例外也是管道:非阻塞字节模式的管道,缓冲区不够的时候,WriteFile 返回的仍是 TRUE,而且 `*lpNumberOfBytesWritten` 会小于 `nNumberOfBytesToWrite`。所以管道上的短写是正常的,到了普通磁盘文件上,它就不适用了,咱们知道有这么个例外,就够用了。

## 指针与尺寸:SetFilePointerEx 与 GetFileSizeEx

读写走的都是当前指针,Win32 挪指针与问尺寸的答案,是一对 Ex 结尾的函数,POSIX 那边的对应物则是 lseek 加 fstat。咱们按补课实验 e1 的输出,把行为的边界一次看全(出自 `e1_pointer_moves.cpp`,咱们用的文件是 10 字节、内容为 ABCDEFGHIJ)。存档里的 [2] 段讲的是查询当前位置的惯用法,咱们在后面的正文里直接给,块里就不重复贴了:

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

三基准的用法(FILE_BEGIN/FILE_CURRENT/FILE_END 配正负偏移),与 lseek 的 SEEK_SET/SEEK_CUR/SEEK_END 完全同构,咱们扫一眼就过。要细看的是后三段。越过 EOF 移动指针是合法的,GetFileSizeEx 报的尺寸纹丝不动,而在那个位置上 ReadFile,给的是 TRUE 加 0 字节,EOF 在这里的待遇不是错误,与咱们上一节的口径正好互扣。真正会失败的只有 FILE_BEGIN 配负偏移:错误码 131 对应的是 ERROR_NEGATIVE_SEEK,而且指针停在原地不动,也不会半途滑到别的位置。查询当前位置的惯用法也在这里补上:拿距离 0 加 FILE_CURRENT 的组合,只查而不动指针。

### 越过 EOF 再写:洞,以及两家的占盘差异

越过 EOF 的指针接着往下写,中间空出来的区段就是**洞**(hole),读出来的全是零。e1b 的实测:头部写 2 字节的 AB,指针跳到 1 MiB 处写 1 字节的 Z,GetFileSizeEx 报的就是 1048577,而读回的前 16 字节是 `41 42 00 00 ...`,AB 后面读到的全是零,洞尾接的才是 `5A`(Z 的十六进制)。这一半的行为,两家文件系统是一致的,真正分岔的在占盘上,咱们把三个读数摆到一起:

| 读数 | NTFS 默认(非稀疏) | NTFS 开 FSCTL_SET_SPARSE 后(DeviceIoControl 下发的稀疏开关) | ext4(e1e,WSL 侧对照) |
| --- | --- | --- | --- |
| 逻辑尺寸 | EndOfFile=1048577 | 1048577 | st_size=1048577 |
| 实占空间 | AllocationSize=1052672 | 131072 | st_blocks=16(折 8 KiB) |

同样一份 1 MiB 的洞加两头实写,NTFS 在默认状态下把整段都记进了 AllocationSize,洞区是照样占盘的。咱们拿 `DeviceIoControl` 发一个 `FSCTL_SET_SPARSE` 把稀疏的开关打开,同一个洞的 AllocationSize 就掉到了 131072,只算两个实写区段了。ext4 天生给的就是表里第三列的待遇,e1e 在同一部机器的 WSL 里跑,st_blocks 的读数是 16,8 KiB 就装下了 1 MiB 加 1 的文件。AllocationSize 的两个数(1052672 与 131072)是当轮 NTFS 分配策略在本机上的观测值,您复跑的时候数量级应该一致,数值是可能不同的。一句话收拢:洞读出来的都是零,而省不省空间,ext4 是天生省的,而 NTFS 那边,得显式开了稀疏开关才省。咱们跨阵营搬代码的时候,最容易在这里被磁盘占用的数字迷惑。

> e1b 里还有一个与文档口径不一致的发现:`FILE_FLAG_NO_BUFFERING` 的句柄越过 EOF 写,官方的 File Buffering 文档说零填充要靠缓存管理器、非缓冲写可能吃 ERROR_INVALID_PARAMETER(87),而本机实测 ret=1、写进去了,洞读回来的也是零。咱们如实记下,您也别按老文档去赌这个错误路径。

### 尺寸挂在文件上,指针挂在文件对象上

问尺寸的路子,Windows 这边给的不止一条。e1c 一口气问了四路:GetFileSizeEx、GetFileInformationByHandle 的 64 位拼装、GetFileInformationByHandleEx 的 `FileStandardInfo.EndOfFile`,外加老一辈的 32 位 GetFileSize。咱们把读数对了一遍,四路全部都是一致的,存档 [1] 段的抬头只数了三路新式读数,老 32 位版是咱们外加的。更有意思的是同一文件的两个句柄:句柄 B 在 1 MiB 处写 1 字节,咱们再用句柄 A 原地重读,四路读数齐刷刷变成了 1048577。尺寸挂在了文件身上,谁开着的句柄都看得见。BY_HANDLE_FILE_INFORMATION 还额外给了身份字段:两次 CreateFileW 拿到的 nFileIndex 相同(002300000001B2AD,这个值是每台机器各不同的,看点在两次的相同),证明它们开的就是同一个文件。咱们把 e1c 的输出贴出来对着读:

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

咱们看 [2] 段的四路:B 句柄只写下了一个字节,A 句柄是原地不动的,读数照样齐齐涨到了 1048577,而 AllocationSize 露出来的还是洞的占盘策略。

那指针的行为呢?e1d 与 e1e 在两个系统上做了同一组对照。咱们拿两次 CreateFileW 各开一个句柄,h1 读了 4 字节之后,h2 的指针纹丝不动,从 0 起照样读得到同样的 4 字节。而 DuplicateHandle 复制出来的 h3,诞生的时候就继承了 h1 的位置,h3 读了 4 字节,h1 就被它推着走了,h3 回退了,h1 也跟着退了。存档里 DuplicateHandle 的那一段,咱们原样搬来:

```text
[3] DuplicateHandle(h1 -> h3):同一个文件对象,指针共享
    h3=00000000000000f4 诞生即继承 pos(h3)=6  (h1 刚读完,停在这)
    h3 读到 "6789"
    读后  pos(h3)=10  pos(h1)=10   <- h1 被 h3 的读推着走
          pos(h2)=4                <- h2 纹丝不动
    h3 回退 -4 后 pos(h1)=6  pos(h3)=6  <- h1 跟着回退,共享实锤
```

咱们看 h3,它一落地就站在 h1 刚停的 6 上,读走的 4 个字节,把 h1 推到了 10,而 h2 的 4 从头到尾是没人碰的。e1e 还在同一部机器的 WSL 里,把同一组对照跑了一遍:

```text
$ ./e1e_linux_side
[1] open x2:fd1=3 fd2=4
    fd1 读 4 字节:"0123"  -> fd1 偏移=4 fd2 偏移=0
[2] dup(fd1)=fd3 读 4 字节:"4567" -> fd1 偏移=8 fd3 偏移=8  <- 互相推进
```

咱们对着读:open 出的两个 fd,偏移是独立的,dup 出来的 fd,偏移是共享的,与 Windows 侧的 e1d 逐条都吻合。机制上的对应关系咱们也立起来:Windows 的文件对象,对的就是 POSIX 的打开文件描述(open file description),`CreateFileW` 的每次调用都各造一个新的,DuplicateHandle 与 dup 复制的,只是指向它的表项。所以每次 CreateFileW 都共享指针的说法,咱们在两侧都实测过了,它是不成立的,能共享的只有复制和继承两条来路。

## FlushFileBuffers:WriteFile 返回之后,数据在盘上吗

WriteFile 顺利地返回了,只等于数据进了系统缓存,这句话的完整故事,Linux 侧的 [页缓存与持久性](../../linux/file-io/03-page-cache.md) 已经用 Dirty 计数、kill -9 与计时实验整篇验过一遍:write() 返回了,而盘上的事还没发生。Windows 侧的镜像问题就一句:谁来替咱们把数据推到盘上?答案就是 FlushFileBuffers,它的位置对应的就是 fsync。补课实验 e2 沿用了 L03 那套方法论:同一份 32 MiB 数据走多条持久化路径,写与刷是分开计时的,跑满 5 轮取的中位数,而 `FILE_FLAG_WRITE_THROUGH`(下称 wt)对应的就是 L03 的 O_SYNC:

```text
mode        write_ms   flush_ms   total_ms  write_MiB_s  total_MiB_s
plain          6.772      0.000      6.772         4725         4725
flush          6.831      8.012     14.774         4685         2166
wt            14.975      0.000     14.975         2137         2137
wt_flush      15.882      0.205     16.086         2015         1989
wt_4k            -          -    389.295            -           82
```

plain 与 flush 的差距,就是进缓存与到盘上的差距:写循环本身只用了 6.8 ms(4725 MiB/s,这是缓存的速度),补的那一次 FlushFileBuffers 花了 8.0 ms,总吞吐就掉到了 2166 MiB/s,与 L03 的 plain(6337)对 fsync(4224),讲的是同一个故事。wt 与写完补一次刷的路子则殊途同归:两边的成绩是 2137 对 2166,受制的都是盘的持续写速度,差的只是等盘的时机。wt 的每一笔写都在等盘,flush 攒完了一批,等盘的活留到了收尾。所以 wt 配 4 KiB 小块直接塌方:吞吐掉到了 82 MiB/s,只剩大块口径的二十六分之一,形态与 L03 的 O_SYNC 配 4 KiB(掉到 3 MiB/s)一模一样,小块加同步的代价同样不挑机器。wt 之后补的那次刷只花了 0.205 ms,写穿路径上没什么脏页可刷了。反过来您再想想,plain 的那 6.8 ms 里,持久性的功夫是一点都没下的。

> 快盘上 plain 与 flush 的差距只有 2.2 倍上下,您换机械盘或关掉盘上写缓存的机器,差距就会大得多了。Windows 这边也没有 /proc/meminfo 的 Dirty 可看,咱们做不到 Linux 侧那样的干净观测窗,咱们只能靠每轮删掉文件、靠上一轮已经刷过的盘,把轮间的干扰压低。数字属于笔者的机器,能迁移的只有相对关系。

### fflush、WriteFile、FlushFileBuffers:三层各管一段

混着 C 运行时用的朋友,咱们还得再分出一层。e2b 拿 FILE* 与旁路的 Win32 句柄做了对拍:逐字节的 fwrite 写 100 字节,fflush 以前旁路句柄看到的尺寸是 0,数据还攒在 CRT 的用户态缓冲里,连 WriteFile 的调用都还没发生。等 fflush 过了,看到的尺寸才是 100。而 1 MiB 一块的大块 fwrite 会直通 WriteFile,写完 32 MiB 的当口,旁路句柄看到的就是 33554432。旁路的句柄看不看得到,看的是数据块进没进 OS,而不取决于您调没调 fflush。三层的计时也量出来了:fwrite 写 32 MiB 花了 6.9 ms 进缓存,第一次的 fflush 花 5.0 ms(清 CRT 的残余),第二次就空转成了 0.000 ms,那 9.2 ms 花的才是把数据真正推到盘上。咱们把 e2b 的三段输出接成一块看:

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

fclose 到了收尾这一步,顺手的只是一次 fflush,谁也不会替您调 FlushFileBuffers,这一层得您自己开口。文件映射的那一侧还有个亲戚 FlushViewOfFile,它只负责发起脏页的写回、不等硬件,想要凑齐 fsync 的语义,文档点名的做法是在它后面再补一次 FlushFileBuffers。FlushViewOfFile 的本尊,您到下一篇讲文件映射时就能见到,那边实测的是它单独出场,组合两步的依据,是那一篇引的文档原话。咱们再补一句,FlushFileBuffers 也能吃卷句柄(管理员以 GENERIC_WRITE 打开 `\\.\C:` 得到的就是),刷的是整卷挂起的写,影响的是全系统,没事您别去碰它,本篇的实验只碰了文件句柄。

## 标准句柄:GetStdHandle 与 SetStdHandle

Win32 层重定向的做法,是用 GetStdHandle 去拿当前标准输出的句柄,负责换掉它的则是 SetStdHandle,也就是 Linux 上 dup2 的镜像。但这里有个要实测才看得清的分歧,咱们直接跑。演示程序的动作分三步:起手咱们调 `printf` 打一行,再用 WriteFile 往控制台的句柄打一行,而后才 SetStdHandle 换成文件句柄,拿 `GetStdHandle(STD_OUTPUT_HANDLE)` 给的句柄去 WriteFile。跑下来您会看到,内容确实进了文件,CRT 的 `printf` 却照旧上屏。CRT 走的是自己的 fd 1,根本不看 Win32 的标准句柄表。而 dup2 改的就是 fd 表,连 printf 也一起带走了:

```text
[handle] hello via WriteFile(GetStdHandle)
[printf] this still goes to the console
$ cat redirect.txt
[handle] this goes into redirect.txt
```

(输出里的第一行排在最前面,是因为管道下的 stdout 是全缓冲,printf 攒到了退出才吐,这是咱们捎带的小发现。)咱们收尾的时候,别忘了用 SetStdHandle 把原句柄换回去。至于句柄的继承:SECURITY_ATTRIBUTES 的 bInheritHandle 置 TRUE 的句柄,才能被 CreateProcess 出的子进程继承。POSIX 的 fork 天生全继承,Win32 默认的却是一个都不继承,要什么,咱们就显式开什么。

## dwShareMode:五行矩阵与双向检查

demo1 的第 (2) 行咱们已经见过独占冲突的样子,现在咱们把 e3 的整个矩阵摆开:头一个句柄的访问固定为 GENERIC_READ|GENERIC_WRITE,第二个句柄的 share 给足 R|W|D,咱们只看第二句柄要的访问这一向:

| 第一句柄 share | 要 READ | 要 WRITE | 要 R\|W | 要 DELETE |
| --- | --- | --- | --- | --- |
| 0(独占) | 拒 32 | 拒 32 | 拒 32 | 拒 32 |
| R | 成功 | 拒 32 | 拒 32 | 拒 32 |
| W | 拒 32 | 成功 | 拒 32 | 拒 32 |
| R\|W | 成功 | 成功 | 成功 | 拒 32 |
| R\|W\|D | 成功 | 成功 | 成功 | 成功 |

矩阵里的拒 32,说的都是 ERROR_SHARING_VIOLATION。而且咱们的矩阵把第二个句柄全开在了同一个进程里:共享检查挂在内核的文件对象上,它是不看出身的,咱们自己开的句柄也会被拦下,而 POSIX 的 open 对同进程再开一次没有任何约束,这样的维度,在 Linux 侧是压根不存在的。e3 还做了跨进程的复验:父进程以 share=0 挂住文件,真子进程用事件的两轮握手接上,第一轮的 CreateFileW(READ) 拒的是 32,等父进程放了手,第二轮就开到了句柄,进程边界内外的行为是一致的。

只看矩阵您容易读出半条规则,咱们把另一半补上:检查是双向的。新句柄要的访问,得是每个在场句柄的 share 放行的,而在场句柄正在用的访问,也得是新句柄的 share 装得下的,两道检查都是要过的。e3 的双向段就是这么设计的:第一句柄的访问是 R|W、share 也是 R|W,第二句柄要的只是 READ,咱们给它配上三档 share:

```text
== [2] 双向检查:第二个句柄的 share 也要装得下第一个句柄的在用访问 ==
    (第一句柄:访问 R|W、share R|W;第二句柄只要 READ,但 share 各配一档)
    第二 share=0(独占) 第二个访问=READ       -> 拒绝 err=32(ERROR_SHARING_VIOLATION)
    第二 share=R         第二个访问=READ       -> 拒绝 err=32(ERROR_SHARING_VIOLATION)
    第二 share=R|W       第二个访问=READ       -> 开到 h=000000000000015c
```

三行的判读咱们过一遍:share 给 0 的那一档,拒的就是 32,share 给 R 的那一档,拒的还是 32,因为它装不下第一句柄握着的写访问,等 share 给足了 R|W,才开到了句柄。所以 share 声明的是我允许别人怎么动这个文件,而不是我能开门,这一句要是念反了,您把矩阵怎么读都会别扭。

### 删除语义:FILE_SHARE_DELETE 的有无,分出两个世界

矩阵的最后一列藏着本篇分歧最大的一块:DELETE 访问。share 没有 D 的时候,DeleteFileW 与 MoveFileExW 遭到的都是拒绝,实测的错误码是 32(SHARING_VIOLATION)。不少资料把这里写成了 5(ACCESS_DENIED),至少在笔者的 Win11 26200 上不是的,咱们以实测为准。等句柄关掉了,同一个 MoveFileExW 立刻就成功了,拦的就是句柄本身。

share 给足了 R|W|D 之后,画面就换了一个世界,咱们把 e3 的输出原样搬来:

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

DeleteFileW 成功了,FileStandardInfo 里能直读到一个为 1 的 DeletePending 标志,而咱们手里的句柄,照常 ReadFile/WriteFile(读回来的 9 字节,十六进制解出来的就是 DELETE-M)。那文件名怎么样了?当场就没了:GetFileAttributesW 报的是 err=2,新开的句柄报的也是 err=2,看到的都是 FILE_NOT_FOUND。等最后一个句柄关上了,文件才真正地被回收。这样的行为,与 POSIX 的 unlink 是一路的:名字是立刻摘掉的,开着的句柄吊命到最后一关。老资料里写的 classic delete-pending 形态,说的是名字留到最后一关、新开句柄会吃到 ACCESS_DENIED,而它在笔者的 Win11 26200 上没有出现。您要是赶上系统升级,行为也是可能漂移的,复跑的时候,请以 .out 重跑的结果为准。改名也是同款的待遇:成功了,原句柄跟着文件走了,新名字倒是健在的。

> e3b 顺带验了共享全开也不等于什么都同步:咱们把读端换成 FILE_FLAG_NO_BUFFERING 的直读句柄,文档警告缓冲写的脏页没写到盘上时可能读到旧数据,而前两种加码(写后新开直读句柄、直读句柄跨写保持)在 .out 里都有原始输出,读到的全是新鲜数据。第三种(32 MiB 整段写完读尾页)只在源码注释与 README 里记了一笔,是没有单独进 .out 的,咱们就不把它当实测的口径念。旧数据是一点都没钓出来的。负结果咱们如实入册:文档的警告仍在,您真要混用,咱们劝您把 FlushFileBuffers 做在前头。

## 实战:文件复制器,赢 copy_file 四倍

学完咱们就上手,来写一个 512 MiB 文件的复制器:CreateFileW 开的一对句柄、ReadFile/WriteFile 的搬运、make_big_file 造的源文件、main 里的三轮计时,用的全是今天见过的原语。咱们看核心函数:

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

代码里的三个取舍,咱们挨个说。读端叠了 `FILE_FLAG_SEQUENTIAL_SCAN`,等于告诉了缓存管理器,让它按顺序来做大块的预读,这是大文件拷贝的标准提示。缓冲用的是 1 MiB,循环里反复地复用,不搞每轮的重新分配,两个 unique_handle 放在内层的作用域,CloseHandle 也就被算进了耗时,对比的口径才算公平。实测跑在笔者的机器上:CPU 是 Ryzen 7 9700X,硬盘是 WD_BLACK 的 SN7100 NVMe,系统跑的是 Win11 26200,编译器是 MinGW-w64 的 g++ 16.1.0,缓存是暖的,源文件是刚造完的,数据都还留在系统的缓存里。三轮跑下来的结果:

```text
source: C:\Users\CHARLI~2\AppData\Local\Temp\sysprog-win01\big.bin (512 MiB)
round 1: ReadFile/WriteFile  161.7 ms | fs::copy_file  885.2 ms
round 2: ReadFile/WriteFile  156.5 ms | fs::copy_file  720.8 ms
round 3: ReadFile/WriteFile  161.4 ms | fs::copy_file  866.5 ms
```

手搓的这一版,稳定地跑在 160 ms 上下,大约是 3.2 GiB/s 的速度,而 `std::filesystem::copy_file` 要的可是 720~885 ms,四倍的差距。标准库为什么反而慢?笔者翻了 libstdc++ 的源码:它的 do_copy_file 在 Windows 上,走的是 `_wopen` 加 stdio_filebuf 的流式搬运。源码里其实还备着 sendfile 与 copy_file_range 两条快路径,都是 Linux 上的系统调用,让内核在两个 fd 之间直接地搬数据,用户态的来回也一并省掉。sendfile 的分支,前面挡着的是一个取反的宏判断,`_GLIBCXX_FILESYSTEM_IS_WINDOWS`,Windows 的编译里这个取反不成立,分支就进不来了。轮到 copy_file_range 的分支,依赖的宏是 `_GLIBCXX_USE_COPY_FILE_RANGE`,而 configure 在 MinGW 上跑的时候,压根是不给它定义的。所以 sendfile 与 copy_file_range 两条路,在 Windows 的编译里都不参与。既没有顺序的提示,又多出了两层缓冲。咱们拿 DLL 导入表交叉验证:libstdc++-6.dll 的导入表里,根本没有 CopyFileW 的导入,do_copy_file 和内核的快路径,咱们从头到尾没查到它们照过面。您真想要官方级的快,Win32 还有专门的 CopyFileW 让内核代劳,本文就不展开了。您已经会的这对 ReadFile/WriteFile,写出来的拷贝,就比标准库的默认路径快出了四倍。

## 另一侧怎么看

咱们把两侧摆到一起再对照一遍。fd 是进程文件表里的小整数下标,HANDLE 是不透明的指针值。POSIX 那边讲的是万物皆文件,一套 open、read、write、close 就管到了底,Win32 这边讲的是万物皆对象,每类对象各有自己的 Create 与 Open,收尾的 CloseHandle 通吃。open 的做法是把一切塞进 flags 里一把梭,CreateFileW 分成了 access、share、disposition 三组参数,换来的是 POSIX 没有的共享控制:五行矩阵管的是谁能开门,FILE_SHARE_DELETE 的有无,分出的删除语义一头连着 32,一头近着 POSIX 的 unlink。read 是允许短读的,所以循环要兜底,同步的 ReadFile 读满请求或到 EOF,退出的条件落在 got==0 身上。lseek 与 SetFilePointerEx 在三基准上是同构的,越过 EOF 的写,两侧都会造出读为零的洞,而洞的实占,ext4 是天生不占的,NTFS 则要显式开了稀疏才不占。fsync 的活归 FlushFileBuffers,O_SYNC 的活归 FILE_FLAG_WRITE_THROUGH,小块配同步的塌方,两侧是同款的。dup2 改的是 fd 表,连 CRT 也一起带走了,SetStdHandle 换的只是 Win32 层,CRT 的 printf 理都不理。另一侧的完整故事,请您移步 [POSIX 文件 I/O 那一篇](../../linux/file-io/01-posix-file-io.md),页缓存与持久性的整条证据链,您到 [页缓存与持久性](../../linux/file-io/03-page-cache.md) 里接着看。

<ReferenceCard title="参考资源">
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
