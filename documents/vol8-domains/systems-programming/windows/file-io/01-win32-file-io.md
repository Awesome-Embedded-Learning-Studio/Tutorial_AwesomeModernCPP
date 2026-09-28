---
title: "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
description: "Windows 侧系统编程第一篇:讲明白 HANDLE 与 fd 的哲学差异、CreateFileW 每个参数的取舍——POSIX 没有的 dwShareMode 独占语义尤其值得看;沉淀全系列复用的契约工具 last_error_code/check_win32/unique_handle,实测错误文本、句柄泄漏计数与 ReadFile 读满请求或到 EOF 的同步语义,最后手搓文件复制器,暖缓存下比 std::filesystem::copy_file 快四倍"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 16
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
  - 实战
---

# Win32 文件 I/O:句柄、CreateFileW 与同步读写

咱们从这一篇起,把系统编程卷开进了 Windows 侧。Win32 API 是用户态的一整套 C 接口,住得也挺讲究:窗口消息那一类,住的是 user32.dll,文件、进程、线程这些基础设施,住的则是 kernel32.dll。而 Win7 之后,kernel32 的多数导出只剩下薄转发的身份,真正的实现都搬进了 kernelbase.dll。再往下一层,就轮到 ntdll.dll 的原生 API 出场,由它 syscall 进内核,而文件对象在原生 API 里对应的名字,就叫 NtCreateFile。您亲手调一次 CreateFileW,沿途就会依次路过 kernel32、kernelbase、ntdll,最后才真正进了内核。C++ 这边就省事了,一个 `#include <windows.h>`,全部的声明都拿到了手。咱们再顺手定义 `WIN32_LEAN_AND_MEAN` 与 `NOMINMAX` 两个宏,把那几百个不相干的头文件,连同 `min`/`max` 宏这对讨嫌的老熟人,一并挡在门外。

W 后缀,是又一个绕不开的历史问题。咱们平常见到的 `CreateFile`,其实是个宏:定义了 `UNICODE`,它映射到的就是 `CreateFileW`,字符集走 UTF-16,没定义的时候,映射到的则是 `CreateFileA`,走 ANSI 代码页,在中文系统上跑的就是 GBK。您要是拿 A 版去开一个当前代码页表示不了的路径,它当场就翻车了,所以现代代码一律点名 W 版配 `wchar_t`,咱们这个系列也不例外。至于 `char`、`char8_t` 与宽字符之间的互转,最省心的做法是交给 [std::filesystem::path](https://en.cppreference.com/w/cpp/filesystem/path),本篇就不展开了。

## 句柄:Win32 的"万物皆对象"

POSIX 那边有句名言,叫"万物皆文件":fd 是进程文件表里的小整数下标,0、1、2 生来就被标准流占掉了。Win32 这边走的是另一句,"万物皆对象、各拿各的句柄":文件、进程、线程、事件、互斥体,在内核层面全是对象,文件去 CreateFileW 那里领,进程找 OpenProcess,事件找 CreateEventW,各自发回的都是一个 `HANDLE`。它本质上是个不透明的 `void*` 值,宽度与指针的宽度相同,而 CloseHandle 一个函数,就通吃了所有内核对象的句柄。请您往下看错误路径演示 demo1_error.cpp(下文简称 demo1)的第 (1) 行,能亲眼见到的失败值就是 `ffffffffffffffff`,它正是咱们说的 `(HANDLE)-1`。

::: warning 失败值:INVALID_HANDLE_VALUE 和 NULL 都有
咱们得把各家的失败值摆在一起看:Win32 各 API 的失败值约定不统一,判错判反了,等咱们的就是静默 bug:

- CreateFileW 失败返回 **INVALID_HANDLE_VALUE**(-1),咱们可别拿 NULL 去判
- CreateFileMappingW、CreateEventW 等多数内核对象创建函数,失败返回的又是 **NULL**,咱们得换一边判
- GetStdHandle 出错返回 INVALID_HANDLE_VALUE,而进程没有关联句柄时返回 NULL,两个都占,咱们两种都得防
- ReadFile/WriteFile/CloseHandle 这类 BOOL 返回的,失败就是 FALSE,细节咱们查 GetLastError

咱们动手写判错代码以前,请翻一遍文档的 Return value 段,就别靠背了。
:::

## CreateFileW:参数逐个过

`CreateFileW(L"demo.bin", GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)` 一共给了咱们七个参数,今天值得咱们挨个过的,是夹在中间的五个:头一个 lpFileName 是路径,示例里已经在用它。第七个 hTemplateFile 本篇用不上,传 nullptr 就行。**dwDesiredAccess** 说的是要什么权限:`GENERIC_READ`、`GENERIC_WRITE` 是宏层面的概括,底下还能按位组合出更具体的权限位。**dwShareMode** 回答的是"别人还能不能开这个文件":给 `0`,意思就是独占,肯放行的,就按 `FILE_SHARE_READ`、`FILE_SHARE_WRITE`、`FILE_SHARE_DELETE` 组合着给。需要咱们打起精神的地方在这里:这是 POSIX 完全没有的维度,Linux 上压根不存在"别的进程开着,我就不让你开"的 open 机制。

**dwCreationDisposition** 是一组互斥的值,它和 POSIX 标志的对照,咱们列在下面:

| 值 | 语义 | POSIX 近似 |
| --- | --- | --- |
| CREATE_NEW | 不存在则建,存在则失败(ERROR_FILE_EXISTS) | O_CREAT\|O_EXCL |
| CREATE_ALWAYS | 存在则截断,不存在则建 | O_CREAT\|O_TRUNC |
| OPEN_EXISTING | 必须存在,否则 ERROR_FILE_NOT_FOUND | 不带 O_CREAT |
| OPEN_ALWAYS | 存在则开,不存在则建 | O_CREAT |
| TRUNCATE_EXISTING | 必须存在并清空(要 GENERIC_WRITE) | O_TRUNC(无 O_CREAT) |

**lpSecurityAttributes** 咱们在本篇一律传 `nullptr`,求的就是"默认安全描述符 + 句柄不可继承"这个组合。结构体里真正常被用到的,是 bInheritHandle 这个成员,它的细节,咱们留到句柄继承的那一篇再展开。**dwFlagsAndAttributes** 尾巴上能叠 `FILE_FLAG_*` 与 `FILE_ATTRIBUTE_*`:今天咱们只需要认识 `FILE_FLAG_OVERLAPPED`,它是异步 I/O 的入口,本系列的后续文章会专门请它出场,这里咱们认个脸熟就好。下一篇的文件映射(02 篇),也是从今天这个句柄出发的。

## 错误处理:本系列的 Windows 侧工具箱

GetLastError 给每个线程留了一个槽,官方文档也明说了,失败以后的原话是 `call GetLastError immediately`:任何一个后续的 Win32 调用,哪怕它成功了,都可能把这个槽覆盖掉。所以失败分支的头一行,咱们就该把它装箱进 `std::error_code`。这个装箱的活儿,咱们就交给 `win_util.hpp`:它是 Windows 侧的工具箱,与 Linux 篇的 `errno_code()`、`sys_call()`、`unique_fd` 一一对应,本系列后续的 Windows 篇直接拿来复用,也就不再重定义了:

```cpp
// win_util.hpp —— Windows 侧契约工具(本系列唯一定义处,后续篇直接复用)
#pragma once

#define WIN32_LEAN_AND_MEAN  // 本头必须最先被 include,否则这两个宏可能已被预定义
#define NOMINMAX

#include <functional>
#include <system_error>
#include <type_traits>
#include <utility>
#include <windows.h>

// GetLastError 立即装箱:Win32 错误码挂 system_category
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

// HANDLE 的 RAII:形态与 Linux 篇的 unique_fd 完全同构
class unique_handle
{
public:
    explicit unique_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_handle(unique_handle&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_handle& operator=(unique_handle&& o) noexcept
    {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~unique_handle() { reset(); }

    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept
    {
        if (h_ != INVALID_HANDLE_VALUE) { ::CloseHandle(h_); }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};
```

check_win32 的难点,难在失败值的不统一,上面那个 warning 里咱们已经见过一轮了。模板里用 `if constexpr` 分流:指针、HANDLE 返回的,失败长相就是 NULL 和 INVALID_HANDLE_VALUE,BOOL 返回的,失败就是 0。失败值不统一这桩麻烦,就这样交给了类型系统去认领。接下来咱们把错误路径全部跑一遍,dwShareMode 的独占冲突,实测也安排在这里:

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

咱们在 MSYS2 UCRT64 的 g++ 16.1.0 下编译运行,命令是 `g++ -std=c++23 -Wall demo1_error.cpp -o demo1.exe && ./demo1.exe`,您要是嫌 DLL 依赖烦,加上 `-static` 就可以了。代码用的全是标准 Win32 加标准 C++,在 MSVC 下同样可编:

```text
(1) h=ffffffffffffffff err=2 text=系统找不到指定的文件。
(2) err=32 text=另一个程序正在使用此文件，进程无法访问。
(3) system(32)=??һ???????????????ʹ??ļ?????????????ʷ???
    generic(32)=Broken pipe
```

三行输出,咱们一行一行地看过去。第 (1) 行的 `ffffffffffffffff`,就是 INVALID_HANDLE_VALUE 的十六进制长相,错误码 2 对应的是 ERROR_FILE_NOT_FOUND,FormatMessageW 还贴心地给出了中文系统的人类可读文本。

第 (2) 行,就是 dwShareMode 独占语义的实测:keeper 以 `0` 共享模式把文件挂住,第二个句柄哪怕要的只是 GENERIC_READ,也被拒在了门外,错误码 32 对应的是 ERROR_SHARING_VIOLATION。这待遇,咱们在 Linux 上可从来没享受过。

最有意思的是第 (3) 行。同一个 32,挂在 `system_category` 名下,给的就是 Win32 的错误文本,挂在 `generic_category` 名下,就成了 errno 32(EPIPE)的 "Broken pipe"。两套错误的宇宙,区分它们靠的,就是 category。至于那行乱码,咱们顺着字节找它的来历:libstdc++ 在 Windows 上,`system_category().message()` 吐出来的是 ANSI 代码页(GBK)的字节,笔者的 UTF-8 终端照单全收,就成了这副模样。所以想拿人类可读的文本,咱们别依赖 message(),老老实实用 FormatMessageW 配上 CP_UTF8,才是正路。

## unique_handle:与 fd 一个灵魂的 RAII

拿 unique_handle 和 Linux 篇的 unique_fd 并排看:fd 换成了 HANDLE,close 换成了 CloseHandle,空值 -1 也换成了 INVALID_HANDLE_VALUE,其余的部分一个字都不用改,两大阵营共享的是同一个 C++ 灵魂。句柄泄漏比 fd 泄漏更隐蔽:单进程的配额宽裕,一时半会儿炸不了,只是内核对象会跟着内存,一点一点地漏出去。GetProcessHandleCount 能把进程当前持有的句柄数读出来,任务管理器里的"句柄"列,读的就是它。咱们试跑一把:裸开 5000 个,读一次计数,再逐个手动 CloseHandle。而后换 RAII,开 5000 个做对照。程序骨架与 demo1 的相同,取数的活儿在 `hc()` 里,一行 `check_win32("GetProcessHandleCount", GetProcessHandleCount, GetCurrentProcess(), &n)` 就够了,剩下的,就是两个循环。

```text
baseline                : 95
5000 raw opens, no close: 5095
raw handles all closed  : 95
RAII block alive        : 5095
RAII block exited       : 95
```

裸开的那 5000 个,把计数顶到了 5095,咱们把它们全部关掉,95 就又回来了。RAII 那一组,同样把计数顶到了 5095,而离开作用域的那一刻,unique_handle 的析构会自动把 CloseHandle 做掉,原地满血地回到了 95。靠自觉防泄漏,咱们是防不住的,能靠的,也就只有类型了。

## ReadFile/WriteFile:读满请求,或到 EOF

同步句柄(没开 FILE_FLAG_OVERLAPPED、lpOverlapped 传的是 nullptr)上,ReadFile 的返回条件,文档的原话是 `The ReadFile function returns when one of the following conditions occur: The number of bytes requested is read...`:它会一直阻塞,直到**读满咱们请求的字节数**。而踩到 EOF 的时候,返回值是 TRUE,`*lpNumberOfBytesRead` 给的就是 0,跨过 EOF 的最后一包数据,返回的则是剩余的字节。这一点与 POSIX read 的差异是本质性的,Linux 的 man 2 read(man7.org)原话是 `It is not an error if this number is smaller than the number of bytes requested`:管道、终端、信号打断,都会造成 read 的短读,所以 POSIX 的循环,必须处理"读了,但没读满"这件事。Win32 的同步文件句柄上,这些咱们都不用管。实测里,咱们拿 4096 字节的请求,去读一个 100000 字节的文件:24 次全是满的 4096,第 25 次返回剩余的 1696,第 26 次返回的则是 TRUE 且 got=0。轮到循环要退场的时候,条件就只剩下 got==0 一个了:

```text
file=100000 request=4096: reads=25 full=24 partial=1(last=1696) total=100000
```

WriteFile 同理,`lpNumberOfBytesWritten` 的地址同样要给到位。文档里唯一的例外也是管道:非阻塞字节模式的管道,缓冲区不够的时候,WriteFile 返回的仍是 TRUE,而且 `*lpNumberOfBytesWritten` 会小于 `nNumberOfBytesToWrite`。所以管道上的短写是正常的,到了普通磁盘文件上,它就不适用了,咱们知道有这么个例外,就够用了。

## 标准句柄:GetStdHandle 与 SetStdHandle

Win32 层的重定向,做法是用 GetStdHandle 拿"当前标准输出"的句柄,负责换掉它的,则是 SetStdHandle,也就是 Linux 上 dup2 的镜像。但这里有个要实测才看得清的分歧,咱们直接跑。演示程序的动作分三步:起手调 `printf` 打一行,再用 WriteFile 往控制台句柄打一行,而后才 SetStdHandle 换成文件句柄,拿 `GetStdHandle(STD_OUTPUT_HANDLE)` 给的句柄去 WriteFile。这么跑下来,内容确实进了文件,CRT 的 `printf` 却照旧上屏。CRT 走的是自己的 fd 1,根本不看 Win32 的标准句柄表。而 dup2 改的就是 fd 表,连 printf 也一起带走了:

```text
[handle] hello via WriteFile(GetStdHandle)
[printf] this still goes to the console
$ cat redirect.txt
[handle] this goes into redirect.txt
```

(输出里的第一行排在最前面,是因为管道下的 stdout 是全缓冲,printf 攒到了退出才吐,这是捎带的小发现。)咱们收尾的时候,别忘了用 SetStdHandle 把原句柄换回去。至于句柄继承:SECURITY_ATTRIBUTES 的 bInheritHandle 置 TRUE 的句柄,才能被 CreateProcess 出的子进程继承。POSIX 的 fork 天生全继承,Win32 默认的却是一个都不继承,要什么,咱们就显式开什么。

## 实战:文件复制器,顺带赢 copy_file 四倍

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

代码里的三个取舍,咱们挨个说。读端叠了 `FILE_FLAG_SEQUENTIAL_SCAN`,等于告诉缓存管理器"按顺序来,做大块的预读",这是大文件拷贝的标准提示,缓冲用的是 1 MiB,循环里反复复用,不搞每轮的重新分配,两个 unique_handle 放在内层的作用域,CloseHandle 也就被算进了耗时,对比的口径才算公平。实测跑在笔者的机器上:CPU 是 Ryzen 7 9700X,硬盘是 WD_BLACK 的 SN7100 NVMe,系统是 Win11 26200,编译器是 MinGW-w64 的 g++ 16.1.0,缓存是暖的,源文件是刚造完的,数据都还在系统缓存里。三轮的结果:

```text
source: C:\Users\CHARLI~2\AppData\Local\Temp\sysprog-win01\big.bin (512 MiB)
round 1: ReadFile/WriteFile  161.7 ms | fs::copy_file  885.2 ms
round 2: ReadFile/WriteFile  156.5 ms | fs::copy_file  720.8 ms
round 3: ReadFile/WriteFile  161.4 ms | fs::copy_file  866.5 ms
```

手搓的这一版,稳定在 160 ms 上下,大约是 3.2 GiB/s 的速度,而 `std::filesystem::copy_file` 要的,可是 720~885 ms,四倍的差距。标准库为什么反而慢?笔者翻了 libstdc++ 的源码:在 Windows 上,它的 do_copy_file,走的是 `_wopen` 加 stdio_filebuf 的流式搬运。源码里其实还备着两条快路径:sendfile 与 copy_file_range,都是 Linux 上的系统调用,让内核在两个 fd 之间直接搬数据,用户态的来回也一并省掉。sendfile 的分支,前面挡着的是一个取反的宏判断,`_GLIBCXX_FILESYSTEM_IS_WINDOWS`,Windows 的编译里这个取反不成立,分支进不来。轮到 copy_file_range 的分支,依赖的宏是 `_GLIBCXX_USE_COPY_FILE_RANGE`,而 configure 在 MinGW 上跑的时候,压根不给它定义。于是 sendfile 与 copy_file_range,在 Windows 的编译里都不参与。既没有顺序的提示,又多出了两层缓冲。咱们拿 DLL 导入表交叉验证:libstdc++-6.dll 里,根本没有 CopyFileW 的导入,do_copy_file 与内核快路径,从头到尾没有见过面。真想要"官方快",Win32 还有专门的 CopyFileW 让内核代劳,本文就不展开了。您已经会的这对 ReadFile/WriteFile,写出来的拷贝,就比标准库的默认路径快出了四倍。

## 另一侧怎么看

咱们把两侧摆到一起,再对照一遍。fd 是进程文件表里的小整数下标,HANDLE 是不透明的指针值。POSIX 那边讲"万物皆文件",一套 open、read、write、close 就管到了底,Win32 这边讲"万物皆对象",每类对象各有自己的 Create 与 Open,收尾的 CloseHandle 通吃。open 的做法,是把一切塞进 flags 一把梭,CreateFileW 分成了 access、share、disposition 三组参数,换来的是 POSIX 没有的共享控制。read 允许短读,所以循环要兜底,同步的 ReadFile 读满请求或到 EOF,退出的条件落在 got==0 身上。dup2 改的是 fd 表,连 CRT 也一起带走了。SetStdHandle 换的只是 Win32 层,CRT 的 printf 理都不理。另一侧的完整故事,请您移步 [POSIX 文件 I/O 那一篇](../../linux/file-io/01-posix-file-io.md)。

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
    title="System Error Codes"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes"
  />
  <ReferenceItem
    :id="6"
    title="GetProcessHandleCount function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount"
  />
  <ReferenceItem
    :id="7"
    title="GetStdHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/getstdhandle"
  />
  <ReferenceItem
    :id="8"
    title="std::system_category"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/system_category"
  />
  <ReferenceItem
    :id="9"
    title="std::error_code"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/error_code"
  />
  <ReferenceItem
    :id="10"
    author="GCC libstdc++"
    title="ops-common.h"
    publisher="GitHub"
    url="https://github.com/gcc-mirror/gcc/blob/master/libstdc++-v3/src/filesystem/ops-common.h"
  />
</ReferenceCard>
