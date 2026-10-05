---
title: "文件映射:CreateFileMapping 与 MapViewOfFile"
description: Windows 侧镜像 mmap 的第二篇:CreateFileMappingW 登记、MapViewOfFile 贴视图,偏移按 64 KiB 分配粒度对齐、失败值是 NULL。老四样照旧实测(映射句柄提早关视图照活、FILE_MAP_COPY 私有副本不进文件、FlushViewOfFile 只刷脏页、SetEndOfFile 拦下截短),五场新实验再往下深一层:VirtualProtect 只能在视图授权范围内收紧(FILE_MAP_READ 升 RW 一律 err=87,失败时出参被写成 1 不能信)、SEC_RESERVE 的保留-提交两段式对照 Linux overcommit(真文件句柄配 SEC_RESERVE 在 Win11 上静默退化、文件被零扩展)、PrefetchVirtualMemory 把页拉进 standby list,可没替咱们挡掉工作集缺页(扫已预取区照样 8192 次缺页)、256 MiB 选型基准暖顺序映射快 2.8 倍随机快 5.8 倍,但首触一轮 93 ms 比 ReadFile 还慢,复访才摊得回来,大页 SEC_LARGE_PAGES 探到特权门槛(本机没授权,按未测入档)。写只读视图实测收 0xC0000005
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
---

# 文件映射:CreateFileMapping 与 MapViewOfFile

上一篇咱们已经把 HANDLE 走通了,ReadFile 与 WriteFile 的同步读写也过了手。动笔以前笔者回头算过一次 `ReadFile` 的成本:数据从文件进了系统缓存,咱们还得再多一次拷贝,它才到得了调用方的缓冲区。顺序的大文件读下来,这一趟拷贝等于把内存带宽白白多占了一遍。随机访问就更亏了,咱们每摸几十字节,就得吃一次系统调用的延迟。

Windows 给出的路子,与 Linux 的 `mmap` 同宗,名字叫**文件映射(file mapping)**:把文件的内容直接贴进自家进程的地址空间,咱们拿指针去访问,缓存的页从头到尾只有一份,整条路做到了零拷贝。所以本篇是 Linux 侧 [mmap 内存映射](../../linux/file-io/02-mmap-memory-mapping.md) 的镜像篇,后文咱们简称它 L02,实验思路咱们原样搬过来,换掉的只有 API 这一层。与 Linux 侧的差异比想象中多:偏移对齐从 4 KiB 的页抬到了 64 KiB 的分配粒度,失败值从 `MAP_FAILED` 换成了 NULL,视图建好了以后还想改保护的话,有一条授权的边界,咱们下面一场一场看。

咱们把工具与环境交代清楚。公共工具的分工与 W01 交代的一致:`unique_handle` 这副骨架定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md)里,`last_error_code` 的装箱定义在 [错误处理范式](../../thinking/02-error-paradigm.md)里,思维基石那两篇是它们的唯一定义处,而 `check_win32` 的定义留在 W01,咱们这里直接领来用。实验分两批:起手的四场演示(demo1 到 demo4)跑在 Windows 本机的 `%TEMP%\sysprog-win02\` 下,用的是 MSYS2 UCRT64 的 g++ 16.1.0,flags 给的是 `-std=c++23 -Wall -Wextra -static`,静态链接图的就是免掉 DLL 依赖。后来补做的五场(e1 到 e5)连同全部原始输出,收进了仓库 `code/volumn_codes/vol8/systems-programming/windows/file-io/02-file-mapping/` 里,编号跟着存档的子目录走,与本篇之外的实验号互不相干,出场次序认的是正文而不是编号,您会在 SEC_RESERVE 一节里就碰上 e5,e3、e4 要到后面的两节才出场,您认文件名就不会认错人。e 系列的编译统一是 `-std=c++20 -Wall -Wextra`,基准那一场另加了 `-O2`,捕获的日期是 2026-10-02,机器还是 Win11 26200 的本机。崩溃码回读的口径也随起跑的 shell 变:demo 系列在 MSYS2 的 bash 里跑,咱们看到的是 Segmentation fault 加退出码 139,而 e 系列从 WSL interop 直跑,`$?` 只剩下 3221225477(这个数就是 `0xC0000005` 的无符号十进制,后文咱们还要反复见到它)低 8 位里的 5,全码咱们得在程序里自己打。

## 两步走:登记映射对象,再贴视图

Linux 那边一个 `mmap` 就干完的事,Windows 分成了两步,对应两个内核对象:**`CreateFileMappingW`** 拿着文件的 HANDLE 造出一个映射对象(section object),咱们把页保护与最大长度登记在它身上。**`MapViewOfFile`** 再把这个对象的一段贴进本进程的地址空间。文档的原话说得直白:`"Creating a file mapping object does not actually map the view into a process address space"`,头一个调用做的只是登记,第二个调用走完了,地址才真正到手。这两步各有一组自己的参数,咱们挨个过。

`CreateFileMappingW(HANDLE, nullptr, flProtect, dwMaximumSizeHigh, dwMaximumSizeLow, nullptr)` 一共给到咱们六个参数,需要咱们打起精神的是 `flProtect` 和长度。`flProtect` 定的是页保护:`PAGE_READONLY` 配只读或写时复制的视图,要求文件句柄打开的时候带着 `GENERIC_READ`。`PAGE_READWRITE` 配的是读写视图,句柄要求的则是 `GENERIC_READ|GENERIC_WRITE`。保护级别和句柄权限是不能打架的,这跟 Linux 侧的限制是同一条,那边拿 `O_RDONLY` 的 fd 配上 `PROT_WRITE`、再挂 `MAP_SHARED` 才吃 `EACCES`。`MAP_PRIVATE` 在 Linux 那边倒是不受此限,豁免的镜像就是下面会实测的 `FILE_MAP_COPY`。

长度咱们按 64 位分成两半传。两半都给 0 的时候,映射对象的大小就取当前文件大小。给的比文件大,分岔的方式由页保护决定,咱们看文档原话:`"If an application specifies a size for the file mapping object that is larger than the size of the actual named file on disk and if the page protection allows write access...the file on disk is increased to match the specified size"`。带写权限页保护的映射,直接撑大的就是文件本身。咱们拿只读页保护去造比文件大的映射,就只有被拒绝的份,实测的错误码是 8(`ERROR_NOT_ENOUGH_MEMORY`),到下面的 `demo1` 里见。0 字节的文件,要是再建一个大小会算成 0 的映射,失败是必然的,文档点名的错误码就是 `ERROR_FILE_INVALID`:`"An attempt to map a file with a length of 0 (zero) fails with an error code of ERROR_FILE_INVALID"`。咱们反过来看另一个方向:显式给长度、配上带写权限的页保护,空文件也是能被撑大的。

最后一个参数管的是名字。填了名字,它就成了**命名映射对象**,您在别的进程里按名字也能打开,这就是 Win32 的共享内存。咱们把 `INVALID_HANDLE_VALUE` 当文件句柄传进去,映射干脆就不挂任何文件了,拿系统的页面文件当后备存储,这也是共享内存的惯用套路,下面讲节属性 SEC_RESERVE 的时候还要回到它身上。

`MapViewOfFile(HANDLE, dwDesiredAccess, dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap)` 负责把第二步落地。`dwDesiredAccess` 得跟头一步登记的保护兼容,咱们把三档访问方式整理成一张表:

| 视图访问 | 语义 | 映射对象至少 | 文件句柄至少 |
| --- | --- | --- | --- |
| FILE_MAP_READ | 只读视图,写它就是 access violation | PAGE_READONLY | GENERIC_READ |
| FILE_MAP_WRITE | 读写视图(名字只写 write,实际可读可写) | PAGE_READWRITE | GENERIC_READ\|GENERIC_WRITE |
| FILE_MAP_COPY | 写时复制,MAP_PRIVATE 的镜像 | PAGE_READONLY 即可 | GENERIC_READ |

参数走到偏移的时候,最容易让咱们栽跟头的地方就到了:Linux 只要求 `offset` 是页大小(4 KiB)的整数倍,Windows 的文档却抬高了要求:`"They must also match the virtual memory allocation granularity of the system...To obtain the VirtualAlloc memory allocation granularity of the system, use the GetSystemInfo function"`。它要的是**分配粒度**,x86/x64 给出的数值是 64 KiB,等于页的十六倍。长度给 0 的时候,文档里说的是 `"the mapping extends from the specified offset to the end of the file mapping"`,映射会从偏移一路贴到映射对象的末尾。

::: warning 失败值是 NULL,偏移按 64 KiB 对齐
CreateFileMappingW 与 MapViewOfFile 失败的时候,返回的都是 **NULL**,同时设置的就是 GetLastError。判错的时候,别把上一篇判 `CreateFileW` 的习惯直接搬过来,那边的失败值是 INVALID_HANDLE_VALUE。咱们要是把只按页对齐(比如 4096)的偏移传进去,MapViewOfFile 直接就失败了,错误码 1132 对应的正是 `ERROR_MAPPED_ALIGNMENT`。页大小和分配粒度咱们都不硬编码,咱们用 `GetSystemInfo` 现场拿。
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

(A) 行是最有意思的:`mapping.reset()` 把映射对象的句柄关了,视图照样把文件内容读了出来。文档给它背了书:`"Mapped views of a file mapping object maintain internal references to the object, and a file mapping object does not close until all references to it are released"`。原来视图自己持着对对象的引用,对象要等所有引用都释放了才肯关,所以关闭的次序随咱们安排。这正好对应 Linux 侧映射建立后立刻 `close(fd)` 也不影响映射的做法。唯一讲究次序的反面教材,留到下面讲截短的部分。

咱们把 (B) 与 (B2) 两行放在一起,看到的是页保护分岔的实测:拿只读页保护去造比文件大的映射,直接就失败了,错误码给的是 8(`ERROR_NOT_ENOUGH_MEMORY`)。判据咱们已经摸到了:撑大条款点名要的是 `flProtect` 带写权限,而句柄带没带 `GENERIC_WRITE`,条款的原文里一个字都没提。所以句柄哪怕带上写权限,只要咱们的 `flProtect` 还是 `PAGE_READONLY`,超额的映射就不能指望它成活。咱们把同一个实验换成 `GENERIC_READ|GENERIC_WRITE` 句柄加 `PAGE_READWRITE`,16 字节的文件当场被撑到 1048576 字节,文档那句把文件撑大的条款,没有一个字是虚的。(C) 行测的则是 0 字节文件,1006 正是文档点名的 `ERROR_FILE_INVALID`。

## mapped_view:本篇的 RAII 工具

视图同样属于取得了就必须释放的资源,咱们漏掉一次 `UnmapViewOfFile`,就是一次地址空间的泄漏。按系列的思路把它交给 RAII:`unique_handle` 管的是 `CloseHandle`,`mapped_view` 管的是 `UnmapViewOfFile`,move-only 的骨架与 Linux 侧的 `mapped_region` 同构,那副骨架的完整定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md),咱们这里只贴 Windows 侧的版本:

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

咱们把它和 `mapped_region` 摆在一起,能省事的地方有两处。失败值换成了 NULL,`check_win32` 的指针分支直接可用,不用像 Linux 篇那样手写 `MAP_FAILED` 的判断。而 `UnmapViewOfFile` 那边,咱们只要给基址、不用给长度,视图的大小系统自己记得,连记录取整后长度的那一步都省下了。

## FILE_MAP_COPY:写时复制,眼见为实

`FILE_MAP_COPY` 的承诺与 `MAP_PRIVATE` 如出一辙,文档的原话是:`"When a process writes to a copy-on-write page, the system copies the original page to a new page that is private to the process. The new page is backed by the paging file...The contents of the new page are never written back to the original file and are lost when the view is unmapped."`。咱们不搞空口无凭,照 Linux 篇的剧本对质一次:16 字节的 `AAAABBBBCCCCDDDD`,咱们在 COPY 视图里改 4 字节,拿另开的句柄问文件、再开只读视图看原件。请您留意,咱们手里的文件句柄全程只有 `GENERIC_READ`,却照样写得了 COPY 视图(`demo2.cpp` 节选,`peek_file` 是另开句柄读 16 字节的小封装):

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

咱们拿到的输出,和 Linux 篇那边对应上了:私有视图里写的是 XXXX,文件倒是纹丝不动,新开的只读视图读到的也是原件。咱们还得多问一个只有 Windows 会问的问题:写时复制出来的脏页,万一进程崩了,会不会污染原文件?文档里那句 `never written back...lost when the view is unmapped` 就是答案,私有页压根不挂在原文件的名下,视图一解除它们就没了,原文件从头到尾是完好的。

## FlushViewOfFile:脏页什么时候写进磁盘

读写视图(`FILE_MAP_WRITE`)改的是系统缓存里的页,咱们把可见性和耐久性分开看。咱们在 demo3 里用 `mapped_view` 开读写视图、写 `YYYY` 到偏移 8、另开句柄问文件,最后咱们再刷一次做对照(`demo3.cpp` 的节选,其余辅助函数和 demo2 的相同):

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

咱们看两行输出是一样的,而含义不同。视图与 ReadFile 共用的是同一份缓存,咱们写完不刷,文件那边就已经看得见了。文档对这样的一致性另有保留,写的是 `not guaranteed`,同步句柄上本机实测是可见的,您就当福利用吧,可别把代码写成依赖它的样子。

可见性既然是白得的,`FlushViewOfFile` 操心的就是另一件事:脏页什么时候进磁盘。它只负责发起脏页的写回,却并不等物理写完就返回了。它的位置落在 `msync(MS_ASYNC)` 与 `msync(MS_SYNC)` 的中间,咱们单用它,到不了 `msync(MS_SYNC)` 的程度。它刷的东西也不完整,而文档里还有一句,咱们原样留在这里:`"The FlushViewOfFile function does not flush the file metadata, and it does not wait to return until the changes are flushed from the underlying hardware disk cache and physically written to disk. To flush all the dirty pages plus the metadata for the file and ensure that they are physically written to disk, call FlushViewOfFile and then call the FlushFileBuffers function."`

按原话的说法,文件的元数据(长度、时间戳)它不刷,硬件的盘缓存它也不等。想要 Linux 侧 `fsync` 那样数据加元数据全推的效果,或者想凑齐 `msync(MS_SYNC)` 的等到底语义,咱们就得按文档点名的搭配在 `FlushViewOfFile` 后面补一个 `FlushFileBuffers`,也就是 Windows 侧干 `fsync` 那件事的句柄级刷盘调用,它自己的语义与用法,W01 里有专门的交代。这两步的组合,咱们在本篇没有连跑实测,依据就是上面引的那句原话,单独出场的 `FlushViewOfFile` 倒是在 demo3 里过了手。两步各管了一段,少了哪一步,崩溃都可能停在数据只推了一半的状态。

## VirtualProtect:视图保护有一条授权的边界

Linux 篇里的 `mprotect` 演过降级再升回的全套,映射建好了保护还能动。Windows 的对应物是 `VirtualProtect`,但它在映射视图上有一条自己的上限,文档的原话摆在这儿:`"For mapped views, this value must be compatible with the access protection specified when the view was mapped"`,新的保护得跟 MapViewOfFile 授予的访问兼容。这句话的分量,咱们拿 e1 拍出来看。e1 的头一路(ro-write)把条件摆到了最宽容:文件用读写方式打开,映射对象给的是 `PAGE_READWRITE`,只有视图本身拿的是 `FILE_MAP_READ`。然后咱们试着把视图升成 RW:

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

咱们拿到的 ret 是 0,err 给的是 87(`ERROR_INVALID_PARAMETER`),升级被拒了。咱们把判据复述一遍:映射对象是读写的,而文件也开着写,唯独视图的授权是只读,`VirtualProtect` 认的就是这一层。而文档只说了兼容,没说判的是哪一层,咱们实测的答案是:判据落在 MapViewOfFile 的授权上,而不是映射对象的页保护、更不是句柄权限(FILE_MAP_COPY 视图配只读映射这类混合组合,咱们没测,留白其实就留在这儿,而存档里没有单列)。咱们顺带拿到一个旁证:哨兵 0xCCCCCCCC 被失败调用写成了 1(`PAGE_NOACCESS`),文档对失败时的出参一个字都没承诺。这个出参就是 `VirtualProtect` 的第四个参数 `lpflOldProtect`(旧保护的出参,节选里当哨兵的 `garbage` 接的就是它),失败调用的它咱们不能信:ro-write 这一路它被塞了 1,存档里没进正文的另一路失败调用(oldprotect 路),同样拿到了被塞 1 的出参。硬写的下场就是第二行的 `0xC0000005`,咱们本篇还会反复见到它,怎么接住它是下一篇的正文。

第二路(protect-write)咱们把视图换成 `FILE_MAP_ALL_ACCESS`,也就是读写都授权的写法,再走同样的链。咱们把保护从 RW 降到 RO、再降到 NOACCESS、再升回 RW,三步全部成功了,`lpflOldProtect` 每一步都如实带回改前的保护,升回 RW 之后同一条写指令也过了,咱们重开文件读回首字节,读到的就是写进去的 X,改动是直达后备文件的,而不是写时复制:

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

(这里有个重开验证的小教训:原句柄是独占打开的,咱们得把映射和文件句柄都关干净再重开,不然拿到的就是 sharing violation,读回来的就是一片空白。)NOACCESS 这一档还有个补充实验(noaccess-read):咱们改过去之后再读,连读这一下都过不去了,异常码给的还是 `0xC0000005`,记录里 info[0] 给的是 0,和写冲突的 1 分了家,字段的完整口径咱们留给下一篇。

所以工程上的姿势是:要写的区段,咱们起手就用 `FILE_MAP_ALL_ACCESS` 把授权拿足,`VirtualProtect` 咱们只用来收紧,降级和禁访问是随时能做的,升回去也越不了授权划定的边界。对照 Linux 侧的分岔也有意思:mprotect 的判据挂在 fd 的打开模式上(L02 引过 man 页,往只读打开的文件映射加 `PROT_WRITE`,回的是 EACCES),Windows 的判据挂在视图授权上。判的位置确实不一样,只是两边的实证不在一个层级:Windows 这半边咱们用 e1 实测过了,Linux 那半边 L02 给的是 man 页的告诫,那边的实测还没有做。

## SEC_RESERVE:保留在前、提交在后的两段式

Linux 的匿名 mmap 默认走**超额承诺(overcommit)**的路线:内核答应下来的地址总量远超物理内存,页要等到真被摸到的那一下才兑现。Windows 把同一件事拆成了明说的两步:**保留(reserve)**做的是只登记一段地址范围,而页一概不给。存储要等到**提交(commit)**那一步才真的给到手上。`VirtualAlloc` 的 `MEM_RESERVE` 与 `MEM_COMMIT` 是这套两段式的原生入口,而文件映射这边,咱们靠 `SEC_RESERVE` 这个节属性拿到保留态的映射。它得配页面文件后备的映射用,也就是把 `INVALID_HANDLE_VALUE` 当文件句柄传进去的那一路,共享内存的惯用形态也正是它。

e2 的头一路(probe)建了一个 64 MiB 的 `SEC_RESERVE` 映射,咱们不访问任何字节、只拿 `VirtualQuery` 做一次体检:

```text
$ ./e2_sec_reserve.exe probe; echo "exit=$?"
[probe] 保留区状态体检(不访问任何字节)
  SEC_RESERVE 64MiB 映射成功,视图=0000028896020000
  VirtualQuery(区首         +0x000000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x4000000
  VirtualQuery(中部         +0x400000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x3c00000
  VirtualQuery(区尾         +0x3fff000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x1000
exit=0
```

State 三处报的都是 `MEM_RESERVE`,整段 64 MiB 都登记上了。请您多看一眼 Protect 那一列:实测是 0,而不是 `PAGE_NOACCESS`。所以咱们判保留态,判据咱们得看 State,您就别指望 Protect 了。咱们再走第二路(touch)直接去摸它:

```text
$ ./e2_sec_reserve.exe touch; echo "exit=$?"
  SEC_RESERVE 64MiB 映射成功,视图=000001693c7e0000
  VirtualQuery(写入前      +0x000000): State=MEM_RESERVE(0x2000) Protect=raw=0x0            RegionSize=0x4000000
  写 base[0]='Z' ...[unhandled-exception] code=0xC0000005
  ExceptionInformation[0]   = 1  (写访问冲突)
  ExceptionInformation[1]   = 000001693c7e0000  (目标数据地址)
exit=5
```

保留态的页表项压根没建,咱们写一个字节进去,收到的还是 `0xC0000005`。真正把两段式走完的是第三路(commit):咱们拿 `VirtualAlloc(MEM_COMMIT)` 把段 A、段 B 逐段提交在区首和 32 MiB 处,中间留 31 MiB 的空洞:

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

(输出第三行是实验程序自己打的标签,咱们原样保留。)提交之后段 A、段 B 的 `State` 翻成了 `MEM_COMMIT`,保护给的是 `PAGE_READWRITE`,读写回读的校验和 267386880 与期望值分毫不差,而空洞还守着保留态。对空洞再走一次带保留的分配,系统回的是 487(`ERROR_INVALID_ADDRESS`),这段地址已经有主了,咱们再想登记一遍,它是不受理的。这就是 Windows 版的懒分配:咱们要多大的范围,就登记多大的范围,页则按需一段一段地给。

真正的意外在第四路(file-backed)。笔者起手的预期是:`SEC_RESERVE` 配的是真文件句柄,`CreateFileMappingW` 会直接拒绝咱们,实验程序的注释里就是这么写的。文档的真实口径却温和得多:`"This attribute has no effect for file mapping objects that are backed by executable image files or data files (the hfile parameter is a handle to a file)"`,对数据文件后备的映射,文档说的是没有效果。实测在 Win11 26200 上的样子是这样的:

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

咱们把三方的说法对齐:预期的拒绝没有发生,文档说的是没有效果,落到咱们的实测里就是当普通映射用。普通映射的常规照走:视图直接是已提交、可写的,而带写保护的超额映射会把文件撑大,于是 16 KiB 的文件被零扩展到了 1 MiB。所以文档是没有翻车的,翻车的只有笔者的预期:保留被静默兑现了。咱们要记的教训只有一条,却是实打实的:咱们想拿真文件配 `SEC_RESERVE` 做懒分配,是走不通的,它一声不吭地退化成普通映射,连一个错误码都不给咱们。

> 同属节属性的还有 `SEC_LARGE_PAGES`(大页)。e5 顺手探了它的门槛:`GetLargePageMinimum()` 给的粒度是 2 MiB,而大页要 `SeLockMemoryPrivilege` 特权。这里的陷阱很经典,而文档写得明明白白:`AdjustTokenPrivileges` 成功时返回的是非零,但咱们还得看 GetLastError 才知道特权有没有真到手。本机实测到的是 ret=1 加 GetLastError=1300(`ERROR_NOT_ALL_ASSIGNED`,本机登录的用户根本没被授予这个特权),随后的 `VirtualAlloc(MEM_LARGE_PAGES)` 与 `CreateFileMappingW(SEC_LARGE_PAGES)` 双双收了 1314(`ERROR_PRIVILEGE_NOT_HELD`),普通页的对照分配则照常成功。大页路径咱们按需要特权未测来入档,您想开的话,组策略里授了权还得注销重登。

## 预取与让页:PrefetchVirtualMemory 和 OfferVirtualMemory

Linux 篇里的 `madvise` 是咱们给内核递话的通道。Windows 这边递话的入口是一组调用。管预取的是 `PrefetchVirtualMemory`,管让页与收回的是 `OfferVirtualMemory` 与 `ReclaimVirtualMemory`,管主动弃页的是 `DiscardVirtualMemory`。它们进来的年代分两拨:Prefetch 是 Windows 8 的,Offer、Reclaim、Discard 是 Windows 8.1 Update 的,MinGW UCRT64 的头文件里都有声明,咱们可以直接链接,e3 的 probe 拿头文件直取地址和 `GetProcAddress` 交叉验证过,两边是一致的,咱们不需要动态解析。真正会绊一下的是枚举名:MinGW 头里写的是 `VmOfferPriorityVeryLow` 这一套,SDK 文档写的是 `VMOfferPriorityVeryLow`,就中间那个 m 的大小写不同,您按文档抄代码,在 MinGW 上编不过。

预取的文档口径,咱们摆在头里:`"The prefetched memory is not added to the target process' working set; it is cached in physical memory"`。咱们这里得请出两个词。**工作集(working set)**是 Windows 划给每个进程的那批物理页,正在用的页都在里面,而系统轻易不往外挤。预取来的页不进工作集,它们去的是**后备列表(standby list)**:那里放的是内容还在物理内存、却已不属于任何进程工作集的页,而它们处在转换态,再被访问时走的是**软缺页(soft fault)**,而页早就在物理内存里,缺的只是挂回页表那一步,连等盘都省了。文档的原话只说到缓存在物理内存,没点名页去了哪儿,standby list 这个落点是咱们按通用 Windows 内存管理补上的推断,e3 存档的读法也是这么落的。e3 的 A/B 对照就是这么设计的:64 MiB 的文件映射,咱们把前 32 MiB(A 区)预取,后 32 MiB 的 B 区留着当对照,咱们再把两区各扫一遍,缺页与耗时都记了下来:

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

暖缓存下咱们看到 A/B 两区的耗时一模一样,咱们一边量到 0.026 s,另一边也是同样的 0.026 s,缺页也都是 8192 上下的量级。预取没有替咱们挡掉工作集的缺页,扫已预取的区,页照样一页一页地挂进来,省下的是磁盘往返,缺页计数倒是半点没省。文件本来就在缓存里的时候,预取自然就什么都省不下来了。冷口径才有差:咱们用 `FILE_FLAG_NO_BUFFERING`(带上它的读写会绕开系统缓存直进盘)重建了同一个文件再跑,扫 B 用了 0.034 s,扫 A 只用了 0.024 s,A 比 B 快了约三成。请您留意这个数字的量级:本机的 NVMe 盘本来就快,预取能省下的那部分磁盘往返在这里很小,可观察的上限就这么多,咱们如实入档,倍数就不吹了。还有个小观察:进程的读 IO 计数器看不见文件映射的调页,两个区记的都是 0。

让页那边(e3 的 offer 路)咱们换私有内存来演:32 MiB 的内存用 `VirtualAlloc` 提交,填好了校验模式,然后咱们 offer 出去再 reclaim 收回来,一连做了八轮:

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

offer 之后咱们看到工作集从 36 MiB 直落到了 4 MiB,这是全表里最稳定的可观察量。八轮 reclaim 的内容校验全部完好,但咱们得把文档的保留意见带在身上:`"The data in reclaimed pages may have been discarded, in which case the contents of the memory region is undefined and must be rewritten by the application"`,而 reclaim 成功也不代表内容一定还在,系统在内存有压力时是真能丢的。好在 reclaim 的返回码把这两路分得开:`ERROR_SUCCESS` 报的是收回且内容完好,`ERROR_BUSY` 报的是收回成功、内容已被丢,e3 每轮打出的 `reclaim ret=0(0=内容保留)` 用的正是这个区分,八轮拿到的都是前者。丢弃的路径得靠真实压力才走得通,单机咱们不硬造,咱们如实记下 8/8。`DiscardVirtualMemory` 则是确定性的自弃,咱们调用之后再读回来,8192 页读到的全是零。咱们对照 Linux 侧,`PrefetchVirtualMemory` 的位置像 `MADV_WILLNEED`,Offer 把取舍权交给系统的做派像 `MADV_FREE`,而 Discard 之后读回全零,与 L02 转述 man 页的 `MADV_DONTNEED` 私有匿名清零口径,行为是对得上号的。还有一条文档细节值得咱们记下:offered 的页不会被写进页面文件。MEM_RESET 系列的遗忘式重置(VirtualAlloc 提供的那一组)就没有这么干脆,页只是标成了可弃,内容是不保的。

## ReadFile 还是映射:256 MiB 同一文件的计时表

开篇欠的那场对比,现在咱们来兑现。e4 拿同一个 256 MiB 的文件,咱们让 ReadFile 与映射把顺序整读、随机 4 KiB ×10000 各跑一遍,再加一路绕过缓存的冷读对照,咱们每场跑 3 轮取中位数,计时用的是 `QueryPerformanceCounter`(Windows 的高精度单调计时器),内容咱们按页号确定性生成,两法的校验和必须一致,顺序场景的 34225520640、随机场景的 5237682176 全都对上了,咱们读的确实是同一份数据。

机器口径咱们交代在数字前面:Win11 26200 的本机,就是 W01 里出场过的同一台机器(Ryzen 7 9700X,后备盘是 WD_BLACK 的 SN7100 NVMe,盘型是 PowerShell `Get-PhysicalDisk` 落的),机器的物理内存 61.7 GiB,空载的时候可用约 35 GiB。暖缓存下全表跑的都是内存速度,内存紧张的机器上您别指望复刻同样的暖数字。这套口径跟 Linux 侧 L02 的双机基准是一个传统:那边立下的口径是数字只在出过基准的两台机器上成立,咱们这边的数字同样只在本机成立,而相对关系比绝对值更有参考价值。编译这次加了 `-O2`。

| 场景 | 中位 | 带宽 / 均摊 | 备注 |
| --- | --- | --- | --- |
| ReadFile 顺序整读(1 MiB 块) | 51.08 ms | 5012 MiB/s | 暖缓存 |
| MapViewOfFile 顺序扫 | 18.18 ms | 14081 MiB/s | 暖,轮 1 含首触 93.45 ms |
| ReadFile 随机 4 KiB ×10000 | 31.93 ms | 3.19 µs/op | 暖缓存,SetFilePointerEx 加 ReadFile 两次系统调用一趟 |
| MapViewOfFile 随机 4 KiB ×10000 | 5.54 ms | 0.55 µs/op | 暖,轮 1 含首触 15.27 ms |
| ReadFile 顺序(NO_BUFFERING) | 82.20 ms | 3114 MiB/s | 绕过缓存的冷读对照,全表最慢 |

顺序一档的骨架代码,咱们把两边贴在一起看最直观(`e4_readfile_vs_map.cpp` 节选):

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

暖顺序映射快了 2.8 倍(18 对 51 ms),赢的就是少那一次内核到用户缓冲的拷贝。随机 4 KiB 的映射快了 5.8 倍(0.55 对 3.19 µs/op),赢的是指针即访问对 seek 加 read 两次系统调用,而数据越零碎,映射的优势就拉开得越大。真正值得咱们多看一眼的,是 map-seq 的轮 1:成绩是 93.45 ms,比 ReadFile 的任何一轮都慢。咱们要摸的 65536 个页哪怕全是软缺页(文件就在缓存里),一轮下来每个缺页摊了约 1.4 µs。当然这 93.45 ms 里还含着扫描本身约 18 ms 的暖耗时,把它扣掉了,缺页的净开销约 75 ms,每页的边际成本约 1.15 µs,量级倒是没变,首触这一轮的开销是实打实的。映射的成本要靠复访摊回来:同一份数据只读一遍就走,而映射反而吃亏,复访越多、访问越碎的时候,映射的优势才立得住。冷读的对照也说明同一件事:绕过缓存的 ReadFile 整读 82.20 ms,与首触的 93 ms 同一量级,一路是真的从盘上搬数据,而另一路补的只是页表,冷场景下两种读法的差距,远没有暖场景那么戏剧化。

Linux 侧的对照咱们也摆上来:L02 的 512 MiB 基准跑了两台机器,台机的 read 赢了约 15%,笔记本上 mmap 赢了约四分之一,可观测的解释是两机缺页单价差约五倍。而 Windows 本机给出的又是第三个答案:暖顺序映射大胜。三个答案摆在了一起,教给咱们的是同一件事,胜负是跟着缺页单价与缓存口径走的,您自己手里的机器值哪个答案,您拿 e4 量一遍,几分钟的事。那什么时候用映射?随机访问、反复访问、跨进程共享、想要零拷贝,这些都是它的主场。而只读一遍的顺序大扫描、想要跨平台行为一致、不想伺候异常的场合,换 `ReadFile` 是更省心的。

## 文件在背后被截短?Windows 根本不让砍

Linux 篇里最惊悚的实验是 SIGBUS:映射挂得好好的,文件在背后被 `ftruncate` 砍掉了一半,咱们再去摸越界区,进程当场就被信号带走了。这一回咱们把同一剧本搬到 Windows:准备 128 KiB 的文件和 `PAGE_READWRITE` 的映射,咱们再让第二个 `GENERIC_WRITE` 句柄把 `SetFilePointerEx` 走到 64 KiB、执行 `SetEndOfFile`(`demo4` 节选):

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

咱们要的答案就在第 (2) 行:**砍不动**。拿到的 1224 就是 `ERROR_USER_MAPPED_FILE`,`SetEndOfFile` 的文档把要求写得毫不含糊:`"If CreateFileMapping is called to create a file mapping object for hFile, UnmapViewOfFile must be called first to unmap all views and call CloseHandle to close the file mapping object before you can call SetEndOfFile."`。POSIX 那边放手让咱们砍,砍完去摸越界的内存,进程就被 SIGBUS 带走了。Windows 则在文件系统的这一层直接拦下,咱们想把文件截短,就得把所有的视图解掉、把映射对象关掉。第 (1) 行还验证了偏移的要求:4096 明明页对齐了,还是被拒了,拿到的 err 1132 就是 `ERROR_MAPPED_ALIGNMENT`,它要的其实是 64 KiB 粒度。

(3)~(5) 行把页尾的语义补齐了:16 字节文件的视图,EOF 之后、页之内的那一段,读出来的是零。咱们写进去也不报错,咱们接着刷 `FlushViewOfFile`,看到的文件还是 16 字节、内容原样,写了也永远到不了文件。这与 Linux 侧的页尾语义完全同一套:文件末尾不满一页的部分,读出来的是零,写了它也不会进文件。

咱们在前面的访问表里已经见过 access violation 这个词,e1 的硬写也已经领教过一回,现在咱们把它真正拍下来。而在 Windows 那儿,它属于结构化异常(structured exception)里的一种。截短的剧本既然在文件系统那一层就被拦下了,那结构化异常是不是就用不上了?其实不是。文档里明说了:`"To guard against EXCEPTION_IN_PAGE_ERROR exceptions, use structured exception handling to protect any code that writes to or reads from a memory mapped view of a file other than the page file"`。而网络断连、磁盘满、设备故障,页调度器拿不回数据的那些时刻,异常也是照样来的。最常见的当属写只读视图,文档的访问表里写得白纸黑字:`"An attempt to write to the file view results in an access violation"`。咱们实测接一个。笔者得多交代一句:咱们用的 GCC 不认 `__try/__except` 这个语言扩展(MSVC 与 Clang 才支持),咱们就改用 `AddVectoredExceptionHandler` 挂上向量化处理器,记录完一笔就放行了,让进程以自身的异常码终止。分发链的完整机制,下一篇咱们专门走完(`demo2` 的续集,hex 打印的辅助函数略。`in_watch` 负责判断出错地址有没有落进咱们登记要监视的那段视图,这段区间与它的写法同样从略):

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

`faulting address` 打出来的正是视图基址,`0xC0000005` 就是 `EXCEPTION_ACCESS_VIOLATION` 的代号。没人接住的结构化异常,会拿异常码自身当进程的退出码,咱们在 bash 里看到的是 Segmentation fault,退出码给的是 139。您换到 cmd 里看 %ERRORLEVEL%,读到的 -1073741819 其实就是 `0xC0000005` 本身。这正是 SIGBUS 默认处理直接终止的 Windows 表亲。异常记录里还写了什么?分发链上谁排在前面?修完现场能不能重放?这些机制的完整交代,全在 [结构化异常:SEH 与 VEH](03-seh-veh.md) 那一篇里等着咱们。

## 另一侧怎么看

咱们把两边对齐着看:fd 换成了 HANDLE,`prot` 分成了 flProtect 与视图 access 的两层,`MAP_SHARED` 与 `MAP_PRIVATE` 对应的是 `FILE_MAP_WRITE` 与 `FILE_MAP_COPY`,而 `msync(MS_SYNC)` 要靠 `FlushViewOfFile` 加 `FlushFileBuffers` 组合出来,单 `FlushViewOfFile` 只发起脏页的写回、不等数据真正落到磁盘,位置在 MS_ASYNC 与 MS_SYNC 的中间。也有不对称的地方,咱们单独过一遍。偏移对齐的基准,从 4 KiB 页跳到了 64 KiB 分配粒度,从 mmap 搬代码过来的朋友,最容易栽的也是这里。步数的安排也不同:Linux 一步 mmap,到了 Windows 分成两步,换来的是把映射句柄提早关掉、视图照样活着的自由,而且两边其实都允许在映射建立以后就把文件句柄关掉。改保护的判据也分了家:Linux 挂在 fd 的打开模式上,Windows 挂在 MapViewOfFile 的视图授权上,e1 实测的 err=87 就是授权边界的样子。懒分配的形态同样分家:Linux 藏在 overcommit 默认里,Windows 做成 SEC_RESERVE 加 MEM_COMMIT 的显式两段式,递话的通道也换了,`madvise` 那边的活儿由 Prefetch 与 Offer 一组接手。两边出事的方式差得更远:POSIX 允许文件在映射底下被砍、事后用 SIGBUS 追责,Windows 的拦截发生在 `SetEndOfFile`,给出的错误就是 `ERROR_USER_MAPPED_FILE`,把事故拦在了发生以前,而 access violation 与 in-page error,留给了权限越界和 I/O 故障。另一侧的完整故事,就放在 [mmap 内存映射:把文件贴进地址空间](../../linux/file-io/02-mmap-memory-mapping.md)。

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
