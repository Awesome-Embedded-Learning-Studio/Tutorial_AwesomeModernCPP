---
title: "平台抽象层设计:从 #ifdef 到 concepts"
description: "同一份代码要活在 Linux 与 Windows 两个平台,差异这层活怎么收才立得住:本篇用四组实验把 #ifdef 与 concepts 摆进同一场对照。预定义宏回答为哪个目标编译(__GLIBC__ 与 _UCRT 把同一家 GCC 的两个 C 库区分开),__has_include 查头不查能力(liburing.h 在只说明用户态库装了),CMake 的平台身份与编译能力在裸 CXX=g++.exe 时分了叉(CMAKE_SYSTEM_NAME 被认成 Linux 而 try_compile 的能力探测反而全对)。死分支实验把三处错误种进 #ifdef 裁掉的 Windows 分支,Linux 编译零警告通过而 Windows 侧六条 error 全现形,是被裁分支零诊断的直接证据。四行一条约束的 ByteSource concept 把 #ifdef 退到选后端的一处 using,缺 read_some 的负例被两侧 GCC 16 在实例化之前拦下并点名。native_handle 用 intptr_t 统一承载 fd 与 HANDLE,Windows 侧 _open_osfhandle 真桥配出 CRT 的 fd、_read 读通 26 字节、_get_osfhandle 还原判等,兑现上一篇留的 uintptr_t 口。虚表对模板的分派成本最好档 0.94ns 与普通调用打平(GCC 16 投机去虚化的汇编在档),最坏档 5.23ns,对 136-157ns 的系统调用锚占 1%-4%,分派成本不该是选型主因"
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 28
prerequisites:
  - "跨平台异步 I/O 抽象"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
related:
  - "跨平台异步 I/O 抽象"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - Win32
  - concepts
  - CMake
  - 零开销抽象
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 平台抽象层设计:从 #ifdef 到 concepts

[总纲](../00-overview.md)铺开的全卷走到这里,两侧各讲各的 API 都收了尾,只剩咱们这一章收卷的活了。[上一篇](03-cross-async-io.md)留了两句交班的话。体面的一句是 AsyncBackend 的五行 concept,它把完成式异步后端的这个形状定在了类型上,同一份用户代码在两个内核上都跑通了。另一句就老实得多了:request.target 拿的 uintptr_t 糙着装 fd 与 HANDLE,说好了留给这一章来收。本篇就是来兑现的,而真正要回答的问题只有一个:同一份代码要活在两个平台,咱们该怎么收差异这层的活,抽象层才算立得住了?

差异这层的活,前辈们最顺手的一招是 #ifdef,咱们把它撒进函数体,每个函数里都留了两个世界。本篇要对照的另一条路,是把差异立到类型的边界上,让 C++20 的 concepts 在编译期把形状约束出来。两条路各自的边界行为是什么,分派的成本又差了多少,咱们用四组实验把话说开:检测、分发、承载、开销,正好各领了一组。

实验的编号与产物位置也交代清楚,免得您翻错了档案。咱们的四组实验按 e1 到 e4 编号,跟着存档的目录走,与上一篇的 e 系互不相干,您认目录就行:全部的代码、逐字的输出与汇编摘录,收在仓库的 `code/volumn_codes/vol8/systems-programming/cross-platform/04-abstraction-design/` 下面,e 打头的文件全是本篇的。输出块的口径也说定了:正文引用的程序输出与编译诊断,都注明了全量还是节选,您对表的时候以存档为准。

三条边界也交代在前面了,免得您等着看不来的东西。上一篇的 AsyncBackend 是引子,那边怎么用 concept 约束一个异步后端、负例的诊断长什么样,讲过的咱们不重讲,本篇把同一套思路扩到了字节源与句柄这些更普通的资源上。Asio 与 libuv 咱们只借接口的形状:io_context 怎么把后端的选择与类型检查安排成混合的式样,libuv 怎么把各平台的句柄包进统一的壳,内部实现就不展开了。工具本身(unique_fd、unique_handle、sys_call 这些)在卷首思维基石的[RAII 范式](../thinking/01-raii-paradigm.md)与[错误处理范式](../thinking/02-error-paradigm.md)两篇里有全子卷唯一的定义处,RAII 与错误装箱的课本篇就不重新上了,把散落的零件收编成库,是下一篇的活。

环境的口径咱们两侧分开说,后面的数字都要拿它对表。Linux 侧出自笔者的 WSL2,内核跑的是 6.18.33.2 的构建,g++ 用的是 16.2.1,编译的命令是 `g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`,拿到的警告数是零。Windows 侧出自笔者的 Win11 26200,用的是 MSYS2 UCRT64 的 g++ 16.1.0,咱们从 WSL 里经 interop 调起,编译的命令是 `/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra`,拿到的警告数同样是零,唯一的例外是 e4 的 -O0 对照版,那是不做优化的一版。CMake 用的是 WSL 侧的 4.4.3。全部输出捕获自 2026-10-05 的同一轮。

## 三条检测路,各答各的问题

抽象层开工要干的头一件事,咱们得弄清楚自己在哪、手里有什么。e1 就排了三条检测的路:编译器的预定义宏,加上 __has_include 的头文件探头,再加上 CMake 的配置期检测。听起来像同一件事的三个说法。

头一路走的是预定义宏。咱们写了一个两侧同一份的探针 `e1_platform_probe.cpp`,把十条宏都打成了一张表,两侧的输出并排摆开:

| 宏 | Linux 侧 | Windows 侧 | 声明的事 |
| --- | --- | --- | --- |
| __linux、__linux__ | DEFINED | absent | 目标平台是 Linux |
| _WIN32、_WIN64 | absent | DEFINED | 目标是 Windows,Win64 仅 64 位 |
| __GLIBC__ | DEFINED | absent | C 库是 glibc |
| _UCRT | absent | DEFINED | C 库是 UCRT(MSYS2 UCRT64) |
| __MINGW64__ | absent | DEFINED | MinGW-w64 工具链 |
| __CYGWIN__ | absent | absent | Cygwin 兼容层,两侧都不在 |
| __APPLE__、__FreeBSD__ | absent | absent | 对照行,macOS 与 BSD 都没有 |

表里最值得您多看一眼的,是 C 库的那一对。咱们两侧用的其实是一家 GCC,差别只落在 C 库上:Linux 侧链的是 glibc,宏给的是 __GLIBC__,Windows 侧的 MSYS2 UCRT64 链的是 UCRT,宏就换成了 _UCRT。所以,预定义宏回答的问题精确到了这一句:编译器正在为哪个目标生成代码,目标里连 C 库的口味都算数。它答不了的问题同样干脆:本机的内核有没有某个机制、处理器有几个,这些咱们都得等程序跑起来才知道。探针的收尾就补上了这一层,Linux 用 uname 拿到了 `sysname=Linux`、`machine=x86_64`,Windows 用 GetSystemInfo 拿到了 `processor_architecture=9`、`number_of_processors=16`,宏永远给不了这些。

第二路咱们请出 __has_include。它在预处理期替咱们回答“这个头在不在包含路径上”,咱们拿五个头当探头,两侧的答案正好互补:Linux 侧 `sys/epoll.h`、`liburing.h`、`sys/inotify.h` 三个在,kqueue 的 `sys/event.h` 与 `windows.h` 缺席,Windows 侧探到的只有 `windows.h`,其余的四个全不在。输出本身倒是平淡,真要防的,是把头文件的存在读成了能力本身。`liburing.h` 探到了,说明的只是用户态的辅助库装上了,它是独立安装的库,内核那边的事它不管:io_uring 的机制内核支不支持,头文件说了不算。头文件是能力的间接判据:头在,实现多半也就到了,版本与内核态的细节都查不到。

第三路的 CMake,配置期它知道的消息最多,奇怪的错也出得最多。咱们在 `e1_cmake_probe` 工程里让 CMake 打印平台身份的变量,再跑四个 `check_include_file_cxx` 的探头。Linux 的原生配置一切正常:CMAKE_SYSTEM_NAME 是 Linux,UNIX 也给的是 1,四个探头给的都是老实答案:epoll 与 liburing 的头在,kqueue 与 windows.h 的头不在,与咱们头文件一路的答案对得上。

坏现场是笔者故意造的。配置的命令换成裸的 `CXX=/mnt/c/msys64/ucrt64/bin/g++.exe`,别的什么都不动,CMake 还是拿宿主的信息回答身份:

```text
e1_cmake_probe/logs/cmake_probe_scenes.txt 场景二(节选):
-- [probe] CMAKE_SYSTEM_NAME = Linux
-- [probe] CMAKE_CXX_COMPILER = /mnt/c/msys64/ucrt64/bin/g++.exe
-- [probe] WIN32= UNIX=1 MSVC= MINGW= MSYS= CYGWIN=
```

身份全错了。g++.exe 生成的分明是 Windows 的目标文件,CMAKE_SYSTEM_NAME 却还答的是 Linux,WIN32 给的是空,UNIX 反倒给了 1。您想,要是 CMakeLists 里写了 `if(UNIX)` 就把 POSIX 的源文件加进目标,它就把带着 `sys/epoll.h` 的代码原样塞给 g++.exe,咱们当场就能收获一次构建失败,下一篇的六场景日志里有这次失败逐字的记录。偏偏在同一个现场里,能力探测倒是对的:`check_include_file` 的内部是一次 try_compile,用的就是真编译器,交回来的答案是 windows.h 在、epoll 不在,与 g++.exe 的真实目标严丝合缝。程序的输出走 config.h,读到的正是这组对的:

```text
e1_cmake_probe/logs/cmake_probe_scenes.txt 场景二运行段(全量):
== route 3: CMake configure-time results (via config.h) ==
  HAVE_SYS_EPOLL_H=0 HAVE_SYS_EVENT_H=0 HAVE_LIBURING_H=0 HAVE_WINDOWS_H=1
```

所以平台身份的答案跟着宿主走,能力探测的答案跟着您手里的编译器走,到了 CMake 这里,它们就是两条线了。本机的配置两边恰好同源,您感觉不出差别,可咱们在 WSL 里调 Windows 的编译器,这就成了半个交叉编译,两边的答案也就跟着分开了。修法是把目标的来源从探测改成声明,写明的动作落在 toolchain 文件里,CMAKE_SYSTEM_NAME 就从探测值变成了声明值,三种写法的对照与六场景的日志都留给下一篇,本篇把分叉的机制记下来就够了。

检测路里还留着一个返工的实录,笔者不打算藏。探针起初想要的是两侧共用一份源文件,在 Linux 的分支里包含了 `sys/utsname.h`,拿到 Windows 侧一编就出了事:头文件在 Windows 上缺席了,直接就编不过了。修法就是把运行期取信息的实现按平台分流,#ifdef 只留了三行:

```cpp
#ifdef _WIN32
    SYSTEM_INFO si {};
    ::GetSystemInfo(&si);
#else
    struct utsname u {};
    ::uname(&u);
#endif
```

这恰好就是咱们对 #ifdef 的立场。它贴着平台的边界分流实现,是它该待的边界,真出了事,错的也是把它撒进业务逻辑的用法,而不是这几行预处理本身。

## 同一个函数,两个世界

检测的路铺完,咱们看分发。e2 的甲场是最常见的一路写法,一个函数里用 #ifdef 留了两个世界:

```cpp
// e2_ifdef_reader.cpp(节选,注释为行文所加)
static std::string read_all_ifdef(const char* path, int& err_out)
{
    err_out = 0;
#ifdef _WIN32
    wchar_t wpath[512];
    ::MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    HANDLE h = ::CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    // ReadFile 循环读,CloseHandle 收尾
#else
    int fd = ::open(path, O_RDONLY);
    // read 循环读,EINTR 重试,close 收尾
#endif
}
```

数据的准备走 std::ofstream,两侧跑的是同一份代码,读取走的是各自的原生 API。咱们两侧各编一份,把两侧的输出并排贴:

```text
Linux 侧(e2_ifdef_linux.out,全量):
[ifdef] backend=POSIX err=0 len=27 match=yes
[ifdef] missing-file err=2 len=0 (POSIX ENOENT=2 / Win32 ERROR_FILE_NOT_FOUND=2)

Windows 侧(e2_ifdef_windows.out,全量):
[ifdef] backend=Win32  err=0 len=27 match=yes
[ifdef] missing-file err=2 len=0 (POSIX ENOENT=2 / Win32 ERROR_FILE_NOT_FOUND=2)
```

读回的内容都对上了,缺失文件的错误值两侧还都是 2。相同的 2 纯属巧合,咱们得说清楚:POSIX 的 ENOENT 是 2,Win32 的 ERROR_FILE_NOT_FOUND 也是 2,两套编号体系是互不相干的。您拿 int 装错误值,两侧碰巧还能对得上了,等您改拿字符串去对 message,一看就分家了。错误模型的统一因此成了另一场硬仗,思维基石的[错误处理范式](../thinking/02-error-paradigm.md)给过零件,这一仗下一篇收 syskit 的时候正面打,本篇的骨架里错误只按编号原样带出。

> 还有一个两侧共用源文件时的细节。windows.h 前面的三行守卫不是装饰,MSYS2 的 libstdc++ 在 os_defines.h 里,已经替咱们定义了 NOMINMAX,您再手动定义一次,-Wall 就会给一句 redefined 的警告,所以外面包了一层 #ifndef。共用一份源文件的两侧,这类小差别总会在意想不到的位置冒出来,守卫写上了,两边就都安静了。

## 死分支:三处错误,本平台一声不吭

甲场跑通了,只证明了正在编译的这一侧是对的。乙场咱们做个狠一点的实验,拿的还是同一个骨架,只在 Windows 的分支里种下三处错。

```cpp
// e2_ifdef_dead_branch.cpp(节选,注释是笔者种的错,原文如此)
DWORDD got = 0;                  // 错误 2:类型拼错,应为 DWORD
if (!ReaddFile(h, buf, sizeof buf, &got, nullptr)) {   // 错误 1:函数拼错
    err_out = static_cast<int>(::GetLastError());
    break;
}
::CloseHandle(h, 2);             // 错误 3:参数个数错
```

咱们把它交给 Linux 侧的 g++,编译交出的是零警告零错误,运行的输出如下:

```text
Linux 侧(e2_dead_linux.out,全量):
[dead-branch] err=0 len=23 match=yes -- 编译时死分支里的三处错误,本平台一声不吭
```

它编过了,还理直气壮地跑通了,读回的 23 字节一个都没差。您可能要问了,三处错误呢?预处理器在 Linux 上把 #ifdef _WIN32 的整个分支裁掉了,裁掉的代码不进编译器,连词法分析的一步都轮不上,拼错的函数名、写错的参数,连被看的资格都没有。同款的文件交给 Windows 侧的 g++,六条 error 全出来了:

```text
Windows 侧(e2_dead_windows.err,节选,要害四条):
../e2_ifdef_dead_branch.cpp:42:5: error: 'DWORDD' was not declared in this scope; did you mean 'DWORD'?
../e2_ifdef_dead_branch.cpp:44:14: error: 'ReaddFile' was not declared in this scope; did you mean 'ReadFile'?
../e2_ifdef_dead_branch.cpp:48:13: error: 'got' was not declared in this scope
../e2_ifdef_dead_branch.cpp:51:18: error: too many arguments to function 'WINBOOL CloseHandle(HANDLE)'
```

咱们种下的三处全现形了,DWORDD 失败还连带着 got 一起没了声明,诊断从三处长成了六条,编译器顺手把 did you mean 的改法也备好了。摆在前面的对照,就是被裁分支零诊断的直接证据。没人编的分支里,错误就能沉睡到有人来的那一天,来的也许是交叉编译的同事,也许是换了 Windows 机器开工的您。项目小的时候,您还可以说两边都编一遍,等项目大了,保证就慢慢变成了祈祷,而祈祷从来不参与编译。

## 四行 concept,把 #ifdef 退到选后端的一处

丙场把同一件事换了个收法。公共的形状咱们用 concept 声明,拢共四行的代码,装下的约束只有一条:

```cpp
// e2_concept_reader.cpp(节选)
template <class S>
concept ByteSource = requires(S& s, void* buf, std::size_t n) {
    { s.read_some(buf, n) } -> std::same_as<std::size_t>;   // 读到多少交回多少,0=到尾
};
```

接下来登场的是两个后端。咱们再配两侧通用的内存源 MemSource,PosixFdSource 与 Win32HandleSource 各自实现的都是 read_some,实现的内部一行 #ifdef 都没有。平台的选择收拢到了一处 using:

```cpp
// e2_concept_reader.cpp(节选,两个后端的实现体此处省略)
#ifdef _WIN32
class Win32HandleSource { /* CreateFileW 开,ReadFile 读,析构 CloseHandle */ };
using NativeSource = Win32HandleSource;
#else
class PosixFdSource { /* open 开,read 读,析构 close */ };
using NativeSource = PosixFdSource;
#endif
```

用户代码咱们只写一份:所有后端用的,都是同一个受约束的模板 drain。

```cpp
// e2_concept_reader.cpp(节选)
template <ByteSource S>
static std::string drain(S& src, std::size_t limit)
{
    std::string out;
    char buf[256];
    while (out.size() < limit) {
        std::size_t n = src.read_some(buf, sizeof buf);
        if (n == 0) break;
        out.append(buf, n);
    }
    return out;
}

static_assert(ByteSource<MemSource>);
static_assert(ByteSource<NativeSource>);
```

咱们把两侧的输出并排贴,连 backends 的一列都只差一个名字:

```text
Linux 侧(e2_concept_linux.out,全量):
[concept] backends={Mem,POSIX} mem_len=28 nat_len=28 match=yes/yes

Windows 侧(e2_concept_windows.out,全量):
[concept] backends={Mem,Win32} mem_len=28 nat_len=28 match=yes/yes
```

丙场的代码里有三处值得您停一下。头一处是 static_assert 那两行:后端被实例化的那一刻一定被检查,断言把这个检查摆到了明面上,Linux 上的 NativeSource 就是 PosixFdSource,被选中的它跑不掉,MemSource 倒是两侧都编,两行的断言两侧都过。第二处咱们看 drain 本身。调用点的写法与运行期多态无异,分派却发生在了编译期,每个后端实例化出各自的版本,内联是可行的,而虚表是不需要的,这就是受约束的静默多态。第三处的分量最重,#ifdef 从函数体里退场了,它现在只活在选谁的一处边界上。drain 的业务代码只有一份,两侧跑的都是零分支,平台的差异被压进了后端自己的实现,而每个后端在自家平台上编译时,自家的断言与调用方自然都会检查它。像乙场那样错误留在死分支里的事,业务层从此没有了。Asio 的 io_context 是同一路数的知名样本,后端作为 service 在运行期是可换的,类型检查仍然留在了编译期,一软一硬地混着用,咱们借的就是这个形状。

光有正例当然不够,丁场咱们配上一个反面。它只提供了 read_fixed,read_some 是没有的:

```cpp
// e2_concept_negative.cpp(节选)
class ShortSource
{
public:
    std::size_t read_fixed(void*, std::size_t) { return 0; }   // 名字不对题
};

static_assert(!ByteSource<ShortSource>,
              "ShortSource 不满足 ByteSource —— 缺 read_some,编译期就该现形");
```

文件里排了两处检查。`static_assert(!ByteSource<ShortSource>)` 编过了,concept 的回答不含糊,如实说了不满足。把一个 ShortSource 实例(存档里叫 shorty)递给 drain 的那一行,则注定是编不过的,咱们两侧都用 `-c` 只编译不链接,要的就是诊断本身。两侧 GCC 16 给的内容一致:

```text
e2_concept_neg_linux.err(节选):
error: no matching function for call to 'drain(ShortSource&, int)'
  • candidate 1: 'template<class S>  requires  ByteSource<S> std::string drain(S&, std::size_t)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • the required expression 's.read_some(buf, n)' is invalid
```

诊断把缺的东西点到了名,连 concept 里那一行的原文都带了出来。两侧的诊断内容是一致的,差的只是引号字形:Linux 侧档案里的引号是弯的,Windows 侧的是直的,上面的摘录咱们按直引号排的版。咱们把它与乙场摆上同一张桌,差别就清楚了:死分支的错误零诊断,暴露的时机要看有没有人把代码带到那个平台,concept 的拦截点是确定的,约束在实例化之前就给了裁决,缺哪一项、缺在 concept 的哪一行,名字都点到了。上一篇 AsyncBackend 的负例咱们引过一回,本篇资源换了,行为的形状一模一样,这本来就是同一条思路在更多资源上的推广。

## 句柄的统一承载:intptr_t 与 CRT 的桥

分发收好了,剩下的是承载。上一篇的 request.target 拿的 uintptr_t 糙着装句柄,笔者说了留给这里收,咱们现在兑现。e3 的 native_handle 用一个 intptr_t 做统一承载,两侧各配了一对进出口:from 接原生句柄,还原交给了 as_fd 与 as_handle。宽度上两侧打出的是 sizeof int=4、intptr_t=8、void*=8,Windows 侧多打了一行 HANDLE=8。

咱们在 Linux 侧的往返是干净的:

```text
Linux 侧(e3_linux.out,全量):
sizeof: int=4 intptr_t=8 void*=8
raw fd            = 3
roundtrip fd      = 3  identical=yes
via void*         = 3  identical=yes
read(roundtripped)= 19 bytes: 'bridge via intptr_t'
```

fd 是 4 字节的 int,装进 8 字节 intptr_t 走的是符号扩展,非负 fd 的高 32 位全零,截断回 int 是无损的。您看 `void*` 的中转也一样干净:整数直接转成 `void*` 是编得过的,只是标准只对 intptr_t 这样够宽的整数保证往返无损,所以 int 的正路是经 intptr_t 中转一道,还原出来的 fd 拿去真读,19 字节读回来了,通道是真的。

真正的桥在 Windows 侧等着咱们:

```text
Windows 侧(e3_windows.out,全量):
sizeof: int=4 intptr_t=8 void*=8 HANDLE=8
raw HANDLE        = 0xb0
roundtrip HANDLE  = 0xb0  identical=yes
_open_osfhandle   -> crt fd = 3 (ok)
_read(crt fd)     -> 26 bytes: 'bridge via _open_osfhandle'
_get_osfhandle    -> 0xb0  identical=yes
```

HANDLE 的本体就是指针宽度,装进 intptr_t 是不存在宽度问题的。咱们真正该看的是中间那座桥:`_open_osfhandle` 给 Win32 的句柄配一枚 CRT(C 运行时)管理的 fd,从此 `_read`、`_close` 这一系的 CRT 函数都能伺候它,一次 `_read` 就读回了 26 字节,反向的 `_get_osfhandle` 从 fd 还原出句柄,还原的值与原值判等。按 Microsoft 文档的口径,这个调用把句柄的所有权移交给了 CRT,关闭走的是 `_close`,句柄也跟着一起关了,您要是再补一句 CloseHandle,就成了重复关闭。fd 的世界与 HANDLE 的世界要互通,走的正路就是这座桥,libuv 在两侧统一句柄的生命周期,走的也是“数据加统一收尾”的形状,内部细节它没让咱们操心。

咱们走到这里,上一篇糙着的 uintptr_t 就有了正经的替身。原生句柄在各自的平台进出,统一层认的就只有 intptr_t,两侧的宽度都装得下,还原判等、真读这些咱们全部实测过。差异其实没有被抽象变没,它只是被关进了 from 与 as_* 几个进出口,其余的代码从此只认 native_handle。

## 虚表对模板:最好打平,最坏五纳秒

最后一组实验回答的,是选型之争里最常被拿出来吵的一条:虚函数比模板究竟慢了多少。咱们在 e4 里把两种多态摆进同一个程序,策略模式的基类加虚函数,对上 concepts 约束的模板静默多态,用的就是前面 read_some 的形状。

测量的防作弊设计,咱们必须交代在前面。-O2 底下单态场景的虚调用会被编译器直接消掉,量出来的是假零,所以两个派生类放进指针数组,循环里按 i&1 的节奏轮流指,动态的类型在编译期不可知,消费函数全部标了 noinline,分派发生的地点在函数体里,整场是吞不掉的:

```cpp
// e4_vtable_vs_template.cpp(节选)
NOINLINE static std::size_t consume_virtual(VirtualSource& s, void* buf)
{
    return s.read_some(buf, 16);      // 间接分派:call 走 vtable 槽
}
```

Linux 侧 -O2 的输出,两轮里咱们取头一轮:

```text
Linux 侧(e4_linux_o2.out,第一轮,全量):
[A] indirect-virtual(predictable) :   0.94 ns/op  (目标交替,BTB 全命中,最好档)
[A2] indirect-virtual(shuffled)    :   5.23 ns/op  (mt19937 乱序,预测失效,最坏档)
[B] static(template)               :   0.84 ns/op  (编译期实例化,直接调用)
[C] plain function                 :   0.84 ns/op  (普通调用锚,对齐 ch00 口径)
[D] getpid anchor                  : 138.23 ns/op  (系统调用锚)
virtual overhead vs plain : best 1.12x worst 6.24x   vs syscall : best 1% worst 4%
```

档位您一看便知。目标按 i&1 规则交替的最好档是 0.94ns,与模板直接调用的 0.84ns、普通调用的 0.84ns 打成了平手。注记里的 BTB 说的就是分支目标缓冲,处理器里缓存间接跳转目标的小表,最好档吃的正是它的命中。输出注记里的 ch00,说的就是咱们的总纲,普通调用锚的口径是在那边立的。mt19937 打乱目标的乱序档掉到 5.23ns,间接分支预测失效的代价全在这几个纳秒里。锚的对照是 getpid 的 138ns,与咱们在总纲里立过的一次系统调用一两百纳秒的直觉再次对上了,虚调用最坏档也只有它的 4%。第二轮的读数(5.16 与 136.14ns)稳在同一量级。-O0 的对照档在存档里,最好与最坏两档的读数是 4.17 与 9.05ns,不优化的世界里差距反倒小了,说明这几纳秒本来就是优化器与分支预测一起做出来的。

那 0.94 为什么能打平?汇编里的答案是现成的。consume_virtual 咱们是标了 noinline 的,可 GCC 16 -O2 在它体内干的事,超出了“老老实实走虚表”的范围:

```asm
// e4_disasm_snippet.txt(x86-64,注释、符号名与地址为行文所改写)
consume_virtual:
    mov    (%rdi),%rax           # 取 vtable 指针
    lea    0x96(%rip),%rdx       # MemVirtualB::read_some 的地址
    mov    (%rax),%rax           # 取 vtable 槽里的目标
    cmp    %rdx,%rax             # 是 B 的实现吗
    je     1740                  # 是,跳去 B 的内联体
    lea    0x217(%rip),%rdx      # MemVirtualA::read_some 的地址
    cmp    %rdx,%rax             # 是 A 的实现吗
    jne    1730                  # 都不是,落回 jmp *%rax 的兜底
    movdqa 0x293a(%rip),%xmm0    # 猜中 A:payload 直接搬进寄存器
    mov    $0x10,%eax
    movups %xmm0,(%rsi)
    ret
```

咱们看编译器干的事:它取了 vtable 的槽,拿槽里的地址与两个 final 派生类的函数地址逐一比对,猜中了哪一个,就把哪个的实现直接内联进来,收场的只有两条 SSE 指令,两个都没猜中的话,才落回了间接跳转。这就是投机去虚化的机制:层级是封闭的、候选就两个,派生类都标了 final,编译器当然敢赌,热点循环里赌中的概率还是高的,分支预测又护了一层。虚函数慢的老话,在封闭层级加现代 GCC 的现场里,咱们得改写成:预测命中的时候与直接调用打平,失效的时候约五个纳秒,而两者都远小于一次系统调用。同场的 consume_static 三条指令收场,连赌都省了。

咱们再看 Windows 侧,同款编译(-O2)跑出的档位同型:最好的 1.04ns,最坏的 4.61ns,模板的 0.75ns,普通的 0.76ns。锚那边却出了怪事,getpid 只计出了 1.32ns。这跟一次普通的函数调用几乎打上平了,系统调用是不可能这个价的。UCRT 的 getpid 疑似纯用户态的实现,像是从 PEB(进程环境块、Windows 给每个进程备在用户态的信息块)里读缓存的 pid,这样的行为官方文档没有明说,咱们也只能存疑。拿它当系统调用的锚,量出来的占比就成了假零。换必进内核的 GetProcessHandleCount,读到的是 157.41ns,与 Linux 侧 getpid 的 136-144ns 同量级,占比回到了 1%-3%。总纲里讲过 glibc 2.3.4 到 2.24 缓存 getpid 的历史,到 2.25 才移除了,Windows 侧的同型现场至今如此。跨平台抽象连基准的锚都要按平台换手,这是 e4 送给咱们的附加一课。

四组实验走完了,选型的答案可以摆正了。虚表与模板的分派差距,最好的档位也就零点几纳秒,最坏的档位五纳秒上下,对一次系统调用 136-157ns 的体量,占的比例是 1%-4%。系统编程的抽象层底下,随便一个真实动作都是系统调用级的开销,分派的成本不该成为选型主因。真正该看的,是 e2 已经量过的那件事:错误暴露的时机,暴露的时候点不点名。诊断的时机与代码组织,才是 #ifdef 与 concepts 之间值得认真吵的两条。下一篇咱们把卷里散落的工具零件收进 syskit 的命名空间,配上零平台分支的 CMake 与冒烟测试,那时再看 #ifdef 退到类型边界的主张,它该还是干净的。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="Constraints and concepts"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/language/constraints.html"
  />
  <ReferenceItem
    :id="2"
    title="requires expression"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/language/requires.html"
  />
  <ReferenceItem
    :id="3"
    title="_open_osfhandle"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/open-osfhandle?view=msvc-170"
  />
  <ReferenceItem
    :id="4"
    title="CMAKE_TOOLCHAIN_FILE"
    publisher="CMake Documentation"
    url="https://cmake.org/cmake/help/latest/variable/CMAKE_TOOLCHAIN_FILE.html"
  />
  <ReferenceItem
    :id="5"
    title="Options That Control Optimization(-fdevirtualize 与 -fdevirtualize-speculatively)"
    publisher="GCC Online Documentation"
    url="https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html"
  />
  <ReferenceItem
    :id="6"
    title="libuv"
    publisher="GitHub (libuv/libuv)"
    url="https://github.com/libuv/libuv"
  />
</ReferenceCard>
