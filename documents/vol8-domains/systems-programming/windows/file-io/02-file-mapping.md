---
title: "文件映射:CreateFileMapping 与 MapViewOfFile"
description: Windows 侧镜像 mmap 的第二篇:文件映射拆成 CreateFileMappingW 与 MapViewOfFile 两步,视图直通系统缓存。本篇过参数与对齐铁律(偏移按 64 KiB 分配粒度,不是 4 KiB 页),实测四件事:映射句柄先关视图照活、FILE_MAP_COPY 的私有副本永不落盘、FlushViewOfFile 只刷脏页不刷元数据、以及 Linux 篇的 SIGBUS 剧本在 Windows 拍不成——文件在映射之下根本砍不动(ERROR_USER_MAPPED_FILE),真正的结构化异常是写只读视图的 EXCEPTION_ACCESS_VIOLATION
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 16
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
related:
  - "mmap 内存映射:把文件贴进地址空间"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 内存管理
  - 优化
---

# 文件映射:CreateFileMapping 与 MapViewOfFile

上一篇里,咱们把 HANDLE 与 ReadFile/WriteFile 走通了。动笔写这一篇以前,笔者回头算了算 `ReadFile` 的账:数据从文件进了系统缓存,还得再付一次拷贝,才到得了调用方的缓冲区。顺序的大文件读下来,这一趟拷贝等于把内存带宽白付了一遍;随机访问就更亏了,咱们每摸几十字节,就得掏一次系统调用的价钱。

Windows 给出的路子,与 Linux 的 `mmap` 同宗,名字叫**文件映射(file mapping)**:把文件的内容直接贴进自家进程的地址空间,咱们拿指针去访问,缓存的页从头到尾只有一份,整条路做到了零拷贝。所以本篇是 Linux 侧 [mmap 内存映射](../../linux/file-io/02-mmap-memory-mapping.md) 的镜像篇,实验思路原样搬过来,咱们换一套 API 重新走一遍。半路上有几个新岔口等着:偏移对齐从 4 KiB 的页抬到了 64 KiB 的分配粒度,失败值也从 `MAP_FAILED` 换成了 NULL。

环境跟上一篇是一样的:代码全部在 Windows 本机的 `%TEMP%\sysprog-win02\` 下,用 MSYS2 UCRT64 的 g++ 16.1.0 编译运行,flags 给的是 `-std=c++23 -Wall -Wextra -static`,静态链接图的就是免掉 DLL 依赖。上一篇定义过的 `last_error_code`、`check_win32`、`unique_handle`,咱们直接拿来用,这里就不再重定义了。

## 两步走:登记映射对象,再贴视图

Linux 那边一个 `mmap` 就干完的事,Windows 分成了两步,对应两个内核对象:**`CreateFileMappingW`** 拿着文件的 HANDLE 造出一个映射对象(section object),咱们把页保护与最大长度登记在它身上;**`MapViewOfFile`** 再把这个对象的一段贴进本进程的地址空间。文档的原话说得直白:`"Creating a file mapping object does not actually map the view into a process address space"`,头一个调用做的只是登记,第二个调用走完,地址才真正到手。这两步各有一组自己的参数,咱们挨个过。

`CreateFileMappingW(HANDLE, nullptr, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, nullptr)` 一共六个参数,需要咱们打起精神的地方,是 `flProtect` 和长度这两位。`flProtect` 定的是页保护:`PAGE_READONLY` 配只读或写时复制的视图,要求文件句柄打开的时候带着 `GENERIC_READ`;`PAGE_READWRITE` 配的是读写视图,句柄要求的则是 `GENERIC_READ|GENERIC_WRITE`。保护级别和句柄权限不能打架,这跟 Linux 侧拿 `O_RDONLY` 的 fd 配 `PROT_WRITE`,再挂上 `MAP_SHARED` 才吃 `EACCES`,是同一条限制;`MAP_PRIVATE` 在 Linux 那边不受此限,这条豁免的镜像,就是下面会实测的 `FILE_MAP_COPY`。

长度按 64 位分成两半传。两半都给 0 的时候,映射对象的大小就取当前文件大小;给的比文件大,分岔的方式由页保护决定,咱们看文档原话:`"If an application specifies a size for the file mapping object that is larger than the size of the actual named file on disk and if the page protection allows write access...the file on disk is increased to match the specified size"`。也就是说,带写权限页保护的映射,直接**撑大**的就是文件本身。咱们拿只读页保护去造比文件大的映射,就只有被拒绝的份,实测的错误码是 8(`ERROR_NOT_ENOUGH_MEMORY`),到下面的 `demo1` 里见。0 字节的文件,要是再建一个大小会算成 0 的映射,也就是两半长度都给 0,失败是必然的,文档点名的错误码就是 `ERROR_FILE_INVALID`:`"An attempt to map a file with a length of 0 (zero) fails with an error code of ERROR_FILE_INVALID"`。反过来,显式给长度、配上带写权限的页保护,空文件也能被撑大。

最后一个参数管名字。填了名字,它就成了**命名映射对象**,您在别的进程里按名字也能打开,这就是 Win32 的共享内存。咱们把 `INVALID_HANDLE_VALUE` 当文件句柄传进去,映射干脆不挂任何文件,拿系统的页面文件当后备存储,这也是共享内存的惯用套路。

`MapViewOfFile(HANDLE, dwDesiredAccess, dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap)` 负责把第二步落了地。`dwDesiredAccess` 得跟头一步登记的保护兼容,咱们把三档访问方式整理成一张表:

| 视图访问 | 语义 | 映射对象至少 | 文件句柄至少 |
| --- | --- | --- | --- |
| FILE_MAP_READ | 只读视图,写它就是 access violation | PAGE_READONLY | GENERIC_READ |
| FILE_MAP_WRITE | 读写视图(名字只写 write,实际可读可写) | PAGE_READWRITE | GENERIC_READ\|GENERIC_WRITE |
| FILE_MAP_COPY | 写时复制,MAP_PRIVATE 的镜像 | PAGE_READONLY 即可 | GENERIC_READ |

参数过到偏移,最容易让咱们栽跟头的地方就到了:Linux 只要求 `offset` 是页大小(4 KiB)的整数倍,Windows 的文档却抬高了要求:`"They must also match the virtual memory allocation granularity of the system...To obtain the VirtualAlloc memory allocation granularity of the system, use the GetSystemInfo function"`。它要的是**分配粒度**,x86/x64 给出的数值是 64 KiB,等于页的十六倍。长度给 0 的时候,文档里说的是 `"the mapping extends from the specified offset to the end of the file mapping"`,映射会从偏移一路贴到映射对象的末尾。

::: warning 失败值是 NULL,偏移按 64 KiB 对齐
CreateFileMappingW 与 MapViewOfFile 失败的时候,返回的都是 **NULL**,同时设置的就是 GetLastError。判错的时候,别把上一篇判 `CreateFileW` 的习惯直接搬过来,那边的失败值是 INVALID_HANDLE_VALUE。咱们要是把只按页对齐(比如 4096)的偏移传进去,MapViewOfFile 直接就失败了,错误码 1132 对应的正是 `ERROR_MAPPED_ALIGNMENT`。页大小和分配粒度咱们都不硬编码,用 `GetSystemInfo` 现场拿。
:::

咱们来跑第一场实测,把两步走、粒度、还有句柄关闭的次序一次看全(`demo1.cpp` 节选,`make_file` 与输出的辅助函数从略):

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

(A) 行是最有意思的:`mapping.reset()` 把映射对象的句柄关了,视图照样把文件内容读了出来。文档给它背了书:`"Mapped views of a file mapping object maintain internal references to the object, and a file mapping object does not close until all references to it are released"`。原来视图自己持着对对象的引用,对象要等所有引用都释放才肯关,所以关闭的次序随咱们安排。这正好对应 Linux 侧映射建立后立刻 `close(fd)` 也不影响映射的做法;唯一讲究次序的反面教材,留到下面讲截短的部分。

咱们把 (B) 与 (B2) 两行放在一起,看到的是页保护分岔的实测:拿只读页保护去造比文件大的映射,直接就失败了,错误码给的是 8,也就是 `ERROR_NOT_ENOUGH_MEMORY`。判据落在页保护上:撑大条款点名要 `flProtect` 带写权限,句柄那边带没带 `GENERIC_WRITE`,条款原文里一个字都没提。所以句柄哪怕带上写权限,只要 `flProtect` 是 `PAGE_READONLY`,超额的映射就不能指望它成活。咱们把同一个实验换成 `GENERIC_READ|GENERIC_WRITE` 句柄加 `PAGE_READWRITE`,16 字节的文件当场被撑到 1048576 字节,文档那句把文件撑大的条款,没有一个字是虚的。(C) 行测的则是 0 字节文件,1006 正是文档点名的 `ERROR_FILE_INVALID`。

## mapped_view:本篇的 RAII 工具

视图同样属于取得了就必须释放的资源,咱们漏掉一次 `UnmapViewOfFile`,就是一次地址空间的泄漏。按系列的思路把它交给 RAII:`unique_handle` 管的是 `CloseHandle`,`mapped_view` 管的是 `UnmapViewOfFile`,move-only 的骨架与 Linux 篇的 `mapped_region` 同构:

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

咱们把它和 `mapped_region` 摆在一起,能省事的地方有两处。失败值换成了 NULL,`check_win32` 的指针分支直接可用,不用像 Linux 篇那样手写 `MAP_FAILED` 的判断;而 `UnmapViewOfFile` 那边,咱们只要给基址、不用给长度,视图的大小系统自己记得,连记录取整后长度的那一步都省下了。

## FILE_MAP_COPY:写时复制,眼见为实

`FILE_MAP_COPY` 的承诺与 `MAP_PRIVATE` 如出一辙,文档的原话是:`"When a process writes to a copy-on-write page, the system copies the original page to a new page that is private to the process. The new page is backed by the paging file...The contents of the new page are never written back to the original file and are lost when the view is unmapped."`。空口无凭,咱们照 Linux 篇的剧本对质一次:16 字节的 `AAAABBBBCCCCDDDD`,咱们在 COPY 视图里改 4 字节,拿另开的句柄问文件、再开只读视图看原件。请您留意,咱们手里的文件句柄全程只有 `GENERIC_READ`,却照样写得了 COPY 视图(`demo2.cpp` 节选,`peek_file` 是另开句柄读 16 字节的小封装):

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

咱们拿到的输出,和 Linux 篇那边对应上了:私有视图里写的是 XXXX,文件倒是纹丝不动,新开的只读视图读到的也是原件。咱们还得多问一个只有 Windows 会问的问题:写时复制出来的脏页,万一进程崩了,会不会污染原文件?文档里那句 `never written back...lost when the view is unmapped` 就是答案,私有页压根不挂在原文件的名下,视图一解除它们就没了,原文件从头到尾一个字节都没动。

## FlushViewOfFile:脏页什么时候写进磁盘

读写视图(`FILE_MAP_WRITE`)改的是系统缓存里的页,可见性和耐久性,咱们得分开看。咱们在 demo3 里用 `mapped_view` 开读写视图、写 `YYYY` 到偏移 8、另开句柄问文件,最后再刷一次做对照(`demo3.cpp` 的节选,其余辅助函数和 demo2 的相同):

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

咱们看两行输出是一样的,含义却不同。视图与 ReadFile 共用的是同一份缓存,咱们写完**不刷**,文件那边就已经可见。文档对这样的一致性另有保留,写的是 `not guaranteed`;同步句柄上,本机实测是可见的,当福利用就好,您可别把代码写成依赖它。

可见性既然是白得的,`FlushViewOfFile` 操心的就是另一件事:脏页什么时候进磁盘。它只负责发起脏页的写回,并不等物理写完;论位置,它落在 `msync(MS_ASYNC)` 与 `msync(MS_SYNC)` 的中间,咱们单用它,到不了 `msync(MS_SYNC)` 的程度。它刷的东西也不完整,文档里有一句,咱们原样留在这里:`"The FlushViewOfFile function does not flush the file metadata, and it does not wait to return until the changes are flushed from the underlying hardware disk cache and physically written to disk. To flush all the dirty pages plus the metadata for the file and ensure that they are physically written to disk, call FlushViewOfFile and then call the FlushFileBuffers function."`

按这句原话,文件的元数据(长度、时间戳)它不刷,硬件的盘缓存它也不等。想要 Linux 侧 `fsync` 那样数据加元数据全推的效果,或者想凑齐 `msync(MS_SYNC)` 的等到底语义,咱们就得按文档点名的搭配,在 `FlushViewOfFile` 之后再补一个 `FlushFileBuffers`,也就是 Windows 侧干 `fsync` 那件事的句柄级刷盘调用。两步各管了一段,少了哪一步,崩溃都可能停在数据只推了一半的状态。

## 文件在背后被截短?Windows 根本不让砍

Linux 篇里最惊悚的实验是 SIGBUS:映射挂得好好的,文件在背后被 `ftruncate` 砍掉了一半,咱们再去摸越界区,进程当场就被信号带走了。这一回咱们把同一剧本搬到 Windows:准备 128 KiB 的文件和 `PAGE_READWRITE` 的映射,再让第二个 `GENERIC_WRITE` 句柄把 `SetFilePointerEx` 走到 64 KiB,然后执行 `SetEndOfFile`(`demo4` 节选):

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

咱们要的答案就在第 (2) 行:**砍不动**。拿到的 1224 就是 `ERROR_USER_MAPPED_FILE`,`SetEndOfFile` 的文档把要求写得毫不含糊:`"If CreateFileMapping is called to create a file mapping object for hFile, UnmapViewOfFile must be called first to unmap all views and call CloseHandle to close the file mapping object before you can call SetEndOfFile."`。POSIX 那边放手让咱们砍,砍完去摸越界的内存,进程被 SIGBUS 带走;Windows 则在文件系统的这一层直接拦下,想把文件截短,就得把所有的视图解掉、把映射对象关掉。第 (1) 行还验证了偏移的要求:4096 明明页对齐了,还是被拒了,拿到的 err 1132 就是 `ERROR_MAPPED_ALIGNMENT`,它要的其实是 64 KiB 粒度。

(3)~(5) 行把页尾的语义补齐了:16 字节文件的视图,EOF 之后、页之内的那一段,读出来的是零;咱们写进去也不报错,接着刷 `FlushViewOfFile`,看到的文件还是 16 字节、内容原样,写了也永远到不了文件。这与 Linux 侧的页尾语义完全同一套:文件末尾不满一页的部分,读出来是零、写它也不会写回文件。

access violation 这个词,咱们在前面的访问表里已经见过一面;在 Windows 那儿,它属于结构化异常(structured exception)里的一种。那结构化异常是不是就用不上了?其实不是。文档里明说了:`"To guard against EXCEPTION_IN_PAGE_ERROR exceptions, use structured exception handling to protect any code that writes to or reads from a memory mapped view of a file other than the page file"`。网络断连、磁盘满、设备故障,页调度器拿不回数据的时刻,异常照样来。最常见的当属写只读视图,文档的访问表里写得白纸黑字:`"An attempt to write to the file view results in an access violation"`。咱们实测接一个。笔者得多交代一句:咱们用的 GCC 不支持 `__try/__except` 这个语言扩展(MSVC 与 Clang 才支持),就改用 `AddVectoredExceptionHandler` 挂上向量化处理器,记录完就放行,让进程以异常码终止(`demo2` 的续集,hex 打印的辅助函数略;`in_watch` 负责判断出错地址有没有落进咱们登记要监视的那段视图,这段区间与它的写法同样从略):

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

`faulting address` 打出来的正是视图基址,`0xC0000005` 就是 `EXCEPTION_ACCESS_VIOLATION` 的代号。没人接住的结构化异常,会拿自身代码当进程的退出码,咱们在 bash 里看到的是 Segmentation fault、退出码 139;您换到 cmd 里看 %ERRORLEVEL%,读到的是 -1073741819,也就是 `0xC0000005` 本身。这正是 SIGBUS 默认处理直接终止的 Windows 表亲。

## 另一侧怎么看

咱们把两边对齐着看:fd 换成了 HANDLE,`prot` 分成了 flProtect 与视图 access 的两层,`MAP_SHARED` 与 `MAP_PRIVATE` 对应的是 `FILE_MAP_WRITE` 与 `FILE_MAP_COPY`,而 `msync(MS_SYNC)` 要靠 `FlushViewOfFile` 加 `FlushFileBuffers` 组合出来,单 `FlushViewOfFile` 只发起脏页的写回、不等数据真正落到磁盘,位置在 MS_ASYNC 与 MS_SYNC 的中间。也有不对称的地方,咱们单独过一遍。偏移对齐的基准,从 4 KiB 页跳到了 64 KiB 分配粒度,从 mmap 搬代码过来的朋友,最容易栽的也是这里。步数的安排也不同:Linux 一步 mmap,到 Windows 分成两步,换来的是把映射句柄提早关掉、视图照样活着的自由,而且两边其实都允许在映射建立以后就把文件句柄关掉。两边出事的方式差得更远:POSIX 允许文件在映射底下被砍、事后用 SIGBUS 追责;Windows 的拦截发生在 `SetEndOfFile`,给出的错误就是 `ERROR_USER_MAPPED_FILE`,把事故拦在了发生以前,而 access violation 与 in-page error,留给了权限越界和 I/O 故障。另一侧的完整故事,就放在 [mmap 内存映射:把文件贴进地址空间](../../linux/file-io/02-mmap-memory-mapping.md)。

## 小结

这一篇靠实测确认下来的事实,咱们收拢在这里,方便您回头查:

- 文件映射走两步:`CreateFileMappingW` 造对象、登记保护与长度,`MapViewOfFile` 贴视图,两者失败都返回 NULL
- 偏移要按分配粒度对齐(实测 65536,`GetSystemInfo` 现场拿),页大小说了不算。长度 0 等于贴到映射对象末尾。映射大于文件时,写权限页保护撑大文件,只读页保护报错,判据是 `flProtect` 而不是句柄权限
- `mapped_view` 做 RAII 收尾,`UnmapViewOfFile` 只要基址不要长度,系统自己记得视图的边界
- `FILE_MAP_COPY` 写时复制:私有页由页面文件支撑,永不写回(实测 XXXX 进不了文件)。只读句柄配 `PAGE_READONLY`,照样写得了 COPY 视图
- `FlushViewOfFile` 只刷脏页、不刷元数据,完整的耐久性要靠 `FlushViewOfFile` 加 `FlushFileBuffers` 两步。可见性是白得的,视图与 ReadFile 共用缓存,实测写完不刷即见
- 截短被系统拦下(`ERROR_USER_MAPPED_FILE` 1224),SIGBUS 剧本在 Windows 拍不成。写只读视图实测收 0xC0000005,进程以异常码退出。页尾零填充、写了到不了文件,咱们读到的东西与 Linux 同语义

<ReferenceCard title="参考资源">
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
</ReferenceCard>
