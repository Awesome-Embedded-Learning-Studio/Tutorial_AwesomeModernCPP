---
title: "syskit 工具库工程化"
description: "vol8 系统编程的收官一篇:思维基石定义的 unique_fd/sys_call/errno_code 与网络卷 networking 的 UniqueFd/SysError 是两套近亲,e1 把两套定义原样复制进同一个翻译单元,detection idiom 探成员、noexcept 探测与标准 trait 打满逐项对照表(骨架同构:4 字节与 nothrow 移动,差异落在 reset 带参、成员 swap 与 is_swappable 双真的探测陷阱、noexcept 标注、EINTR 归宿、错误模型 40 字节带上下文对 16 字节可判等),统一方案落在命名 syskit:: 蛇形加 error_code 双出口,networking 侧影响面逐文件核过(三份文档四处落点,代码侧 01-modern-socket 五个文件加 lab0 脚手架的第二份拷贝与测试五处引用,02 与 03 两篇不引用),e2 把四件工具收编成 INTERFACE 库的 syskit 骨架,四个头逐个标注收编自哪篇,双平台同一份 CMakeLists 零平台分支,冒烟测试两侧各编各跑全过,Windows 侧错误值 2 与 3 的语义分叉和 message() 的 ANSI 代码页字节如实入册,e3 拿 if、generator expression、toolchain 文件三写法六场景量 CMake 平台分支的生效时机,独家实录裸 CXX 覆盖下平台身份认错而编译能力全对的两条线分叉、genex 生成期展开的 ninja -v 命令行证据、make 把 C: 当目标分隔符的增量重编失败,收卷前补一条基准纪律,Windows 侧 getpid 计时 1.3ns 疑从 PEB 读缓存,真基准要换必进内核的 GetProcessHandleCount 的 157ns"
chapter: 8
order: 5
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 27
prerequisites:
  - "平台抽象层设计:从 #ifdef 到 concepts(ch07 第 1 篇)"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "现代 socket 封装:RAII、std::expected,以及每连接一线程扛不住的 C10K(只依赖其工具段)"
related:
  - "平台抽象层设计:从 #ifdef 到 concepts"
  - "跨平台异步 I/O 抽象"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "error_code：错误码体系与自定义 category"
  - "expected：值或错误，C++23 的错误处理新范式"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - 工程实践
  - CMake
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# syskit 工具库工程化

咱们这一卷写到这儿,该收卷了。收卷不是把文章码齐就完事的:unique_fd、sys_call、errno_code 这一整家子的工具,咱们在各站用了一路,定义却还住在思维基石的两篇里,引用的活全靠各篇自己去干。咱们真要把它们收成一个库,就得给它们一个正式的住处、一个两侧都编译得起来的工程。不过在动手之前,有个更麻烦的问题在等着咱们:其实在咱们这个仓库里,同样的工具还存着两套。

网络卷的 01 篇自己定义过 UniqueFd 和 SysError,跟本子卷的工具是近亲,相近的地方很多,不同的地方也不少。您要是网络卷和这一卷都读过,多半隐约地觉出来过。两套并存的局面摆在咱们面前,收库的时候咱们就得正面回答:合不合,怎么合,合了之后那边引用它的文章与代码怎么办。本篇就拿三组实验把这件事办利索:e1 把两套工具放进同一个翻译单元做逐项的对照,e2 搭出 syskit 库的骨架,e3 把构建脚本的平台分支用三种写法各量一遍。

实验编号的口径交代在开头。本篇的实验是小写的 e1 到 e3,跟 [03 跨平台异步 I/O 抽象](03-cross-async-io.md)的 e 系、[平台抽象层设计](04-platform-abstraction.md)的 e 系互不相干,您翻存档的时候认目录就好,本篇的东西全收在 `code/volumn_codes/vol8/systems-programming/cross-platform/05-syskit/` 的下面。输出块的口径也说一句:e1 的输出与 e2 两侧的 smoke(冒烟)输出是全量引用,e3 的场景日志是节选,您对表的时候以存档为准。

环境的口径咱们两侧分开说,后面的输出都拿它来对。Linux 侧出自笔者的 WSL2:内核 6.18.33.2、g++ 16.2.1、cmake 4.4.3。e1 的探针和 e2 的骨架要用 std::expected,编译按的是 `-std=c++23`,优化与警告的口径是 `-O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`,拿到的警告数为零。Windows 侧出自笔者的 Win11 26200,MSYS2 UCRT64 的 g++ 16.1.0,是咱们从 WSL 经 interop 调起来的,cmake 用的还是 WSL 侧的 4.4.3,配的是交叉配对用的 toolchain 文件加 Ninja 生成器,make 的遭遇记在 e3 的场景六里。全部的输出捕获自 2026-10-05 的同一轮。

## 两套近亲是怎么来的

本卷的思维基石立过一个约定:unique_fd、unique_handle、mapped_region、sys_call 全家,整个卷的定义只有一处,后面的各站只引用、不重写。网络卷的成文在约定成立之前,那边的[现代 socket 封装](../../networking/01-modern-socket-wrapping.md)自己定义了 UniqueFd 与 SysError,另立了一套。咱们把两套摆在一起看,各自的代码都工作得挺好,麻烦出在细节已经悄悄地分了叉,而且分叉得还不均匀。

名字的分歧最明显。networking/01 的正文里,类的名字是帕斯卡的 `UniqueFd`,而它的代码档里,文件名却是蛇形的 `unique_fd.hpp`,再翻到 networking/00 结尾的预告,那句话里写的又是小写的 `unique_fd`。同一个工具前后长出了三个样子。您可能觉得这是小事,可咱们接下来要做的是把工具收进一个命名空间统一发放,名字不统一的话,收编之后的每一处迁移都在制造歧义。

## e1:同一个翻译单元里的对照探针

咱们的对照要照得诚实,咱们就不能凭印象列清单、得请编译器自己来交代。e1 的做法是把两套定义原样复制进同一个翻译单元:net:: 命名空间里住着 networking 的 UniqueFd 加 SysError,sysp:: 里住着思维基石的 unique_fd 加 errno_code,出处也标进了注释里,语义则是一字不动地搬了过来。探针分了两层:trait 层拿 detection idiom 探的是成员、noexcept 运算符探的是标注,运行层的做法是让同一个 ENOENT 走两套装箱,看错误对象各自交出什么。

```cpp
// e1_recon_trait_probe.cpp(节选)
template <class T, class = void>
struct has_reset_with_arg : std::false_type {};
template <class T>
struct has_reset_with_arg<T, std::void_t<decltype(std::declval<T&>().reset(0))>>
    : std::true_type {};

template <class T, class = void>
struct has_swap : std::false_type {};
template <class T>
struct has_swap<T, std::void_t<decltype(std::declval<T&>().swap(std::declval<T&>()))>>
    : std::true_type {};

template <class T>
static bool get_is_noexcept() { return noexcept(std::declval<T&>().get()); }
template <class T>
static bool reset_is_noexcept() { return noexcept(std::declval<T&>().reset()); }
```

detection idiom 探的是“成员存在不存在”,noexcept 的探测探的是“标注过没有”,咱们把两样合起来,接口面的差异就藏不住了。输出咱们全量贴上来,您逐行对:

```text
$ g++ -std=c++23 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e1_recon_trait_probe.cpp -o e1 && ./e1
== A. 尺寸与移动语义(两侧同构) ==
  sizeof                             net:4  sysp:4
  is_nothrow_move_constructible      net:1  sysp:1
  is_nothrow_move_assignable         net:1  sysp:1
  is_default_constructible           net:1  sysp:1
== B. 接口面差异(trait 探针) ==
  reset 带参重载 reset(int)      net:0  sysp:1
  成员 swap()                      net:0  sysp:1
  std::is_swappable_v                net:1  sysp:1
  release()                          net:1  sysp:1
  get() 标注 noexcept              net:0  sysp:1
  reset() 标注 noexcept            net:0  sysp:1
== C. 错误模型对拍(SysError vs error_code) ==
  sizeof(错误类型)               net:40  sysp:16
  两侧错误值同源(errno=2)
  net::SysError  携带上下文: "open"
  sysp error_code message(): "No such file or directory"
  error_code == errc::no_such_file_or_directory : true
  SysError errno_value 与 errc 判等(裸 int 比较枚举):
== D. 全链路对拍:open+read 一个不存在的文件 ==
  [net ] 失败: context="open" errno=2 ("哪一步"在错误对象里)
  [sysp] 失败: value=2 message="No such file or directory" ("哪一步"要在调用点/日志层补)
== E. 成功路径换手(/dev/null) ==
  net  moved: src_empty=1 dst_valid=1
  sysp moved: src_empty=1 dst_valid=0  after swap: sw=1 sm=0
```

C 段的末尾有一行悬空的标题、后面没有读数:探针原想演示 SysError 的裸 int 对 errc 枚举的判等,直接比是编不过的,得走手写的映射,说明写在了源码的注释里,输出里就只留下了这行标题。咱们按全量引用的口径,把它原样地入册。

## 一张逐项对照表

咱们把输出连同两份源码读下来,两套工具的关系就能落成一张表。骨架的那一层,两套是完全同构的:4 字节、nothrow 移动、可默认构造,A 段的读数全都相同。差异全都落在了接口面与错误模型上,咱们一条一条列给您:

| 维度 | net::UniqueFd 与 SysError | 本卷 unique_fd 与 sys_call | 评注 |
| --- | --- | --- | --- |
| 定义位置 | networking/01 与其代码档 | 思维基石两篇,全卷唯一定义处 | 一在网络卷自成一篇,一有本卷的约定 |
| 命名 | 帕斯卡 UniqueFd,文件名却是蛇形,预告又写小写 | 蛇形,与标准库同风 | 三处漂移是统一的现成动机 |
| 平台覆盖 | Linux 单侧 | unique_fd 与 unique_handle 双侧配套 | 各管一段,合起来才齐 |
| 尺寸与移动语义 | sizeof 4,nothrow 移动,可默认构造 | 实测完全相同 | 骨架同构,合并的阻力不在这层 |
| reset | 仅无参,关闭加置空 | 带参重载,关旧接管新 | 本卷多一条原地换手的路 |
| swap | 无成员,通用 std::swap 兜底,三次移动 | 成员直换 int,一次交换 | is_swappable 两套都是 true,探测要探成员 |
| noexcept 标注 | get 与 release 与 reset 未标 | 全标 | 不标注等于 trait 层面不承诺 |
| 错误模型 | SysError,sizeof 40,上下文跟错误走 | error_code,sizeof 16,message 与 errc 判等 | 设计分歧,下文专讲 |
| EINTR | 调用循环手写 continue | sys_call 内部重试 | 离库更近一档 |
| 语言标准 | C++23(expected) | error_code 路线 C++20 可用 | 装箱与判等在 C++20 就可用 |

表里最值得您留神的还是 swap 的那一行。net 版是没有 swap 成员的,可 std::is_swappable 探出来的依然是 true,通用版的 std::swap 替它兜了底,代价是实打实的三次移动:一次移动构造、再加两次移动赋值。本卷版的 swap 是成员直换两个 int,一次交换就完事了。[思维基石的 vector 实验](../thinking/01-raii-paradigm.md)里那个 shuffle 零移动,靠的正是成员 swap 在撑。所以您要探测一个类型有没有 swap,得探成员而不是探 trait,trait 交回的 true 其实什么都没保证。

命名与平台覆盖的两行,咱们合起来读。net 版的活动范围只在 Linux 一侧,名字的分歧也就只在 Linux 的代码里可见。本卷的一套从思维基石起就是双平台的,unique_fd 与 unique_handle 是配着写的,蛇形的名字在两侧的标准库里也都有同款。统一进了 syskit:: 之后,您在两侧写下的就是同一个名字,再没有第二套的写法。

noexcept 的那一行也值得您停一停。两边的这些函数在语义上都不会抛,可 net 版的这几个函数没标,所以 trait 层面就是不承诺,noexcept 探测交回的也就是 0。缺了标注,影响的是泛型代码对未标注类型的态度。容器与算法对它的处理,走的是“可能抛”的保守口径。收编进库的时候咱们把该标的标注都补齐,e2 的骨架也照思维基石原样保留了全量的 noexcept 标注,承诺就是这么兑现的。

reset 的带参与 EINTR 的归宿,表里一句话就带过了,背后咱们留了实测在档:带参 reset 的“关旧接管新”,思维基石的 strace 存档演过,EINTR 收进 sys_call 内部重试的完整证据链在[错误处理范式](../thinking/02-error-paradigm.md)。这两方面的差异方向一致:本卷的一套离库的形态更近,收进来要动的地方也更少。

## 错误模型:上下文对判等

表里唯一需要咱们单独展开的是错误模型,因为它是两套真正的设计分歧,不是谁忘了写。D 段的全链路对拍把分歧摆得很直:同样是 open 一个不存在的文件,net 式的失败报告是 `context="open" errno=2`,“哪一步出的事”就装在错误对象的身上。本卷式交回的是 `value=2 message="No such file or directory"`,标准文本是现成的,“哪一步”的信息,您就得靠调用点来补。

SysError 的胜场就是上下文:字符串跟着错误对象走,expected 的错误通道里直接可读,您打日志不用另找信息。代价也是直白的,实测的 sizeof 是 40,里面躺着的是一份 std::string,上下文一旦长了就要动堆,失败路径上的内存分配躲不开。这件事在系统编程里的分量不小。

error_code 的胜场是值语义:16 字节的它,拷贝是零成本的,message() 给的是标准文本,errc 的判等实测为 true,而 Windows 侧的 system_category 还能桥接,升级到自定义 win32_category 的正路,思维基石 02 也演过的。您回头看,error_code 丢的只有“哪一步”,而这一样有两个现成的补法:sys_call 的第一个参数会进 system_error 的 what 前缀,or_else 记的是调用点的日志,两个补法都是思维基石 02 的 E3 演过的。两个机制都有实测的背书,所以统一到 error_code 之后,信息量并没有真的丢。

双出口的约定咱们顺手重申一遍:工具层返回 `expected<T, error_code>`,应用顶层做的则是把错误包成 system_error 抛出去,全程序的 throw 点收敛在 main 附近。error_code 与 expected 的机制课在 vol3 的 [error_code](../../../vol3-standard-library/error-utils/66-error-code.md) 与 [expected](../../../vol3-standard-library/error-utils/64-expected.md) 两篇,咱们只取用,课就不重开了。

## 统一方案与影响面

方案现在可以摆出来了,就两条:命名统一到 `syskit::` 的蛇形,错误模型统一到 error_code 的双出口。笔者不装作这是唯一解:要是您的项目里错误报告深度依赖上下文字符串,SysError 的路线同样站得住。只是在两套的实测都铺好之后,判等与跨平台桥接更硬的那一边,笔者选了 error_code,上下文的缺口拿 what 前缀和调用点日志去补。

影响面咱们逐文件核过,给您完整的清单。文档侧是 networking/00 的预告一句、networking/01 的正文定义与用例,再加 lab0 的前置知识一行与脚手架清单里的 `include/net/unique_fd.hpp`,三份文档里的四处落点。代码侧的引用有两摊:一摊是 `code/volumn_codes/vol8/networking/01-modern-socket/` 下面的五个文件(unique_fd.hpp、echo_server、echo_client、hold_clients 与 README),其中 echo_server 的引用有六处,echo_client 与 hold_clients 的引用加起来三处,README 里也提了一次。另一摊是 `code/volumn_codes/vol8-labs/lab0-mini-reactor/` 的脚手架,第二份 UniqueFd 的完整拷贝,就躺在脚手架的 include/net/unique_fd.hpp,测试文件 `tests/lab0_tests.cpp` 里的引用还有五处。咱们也查过 02-epoll 与 03-reactor,两篇的正文都不引用它。收编的动作仍然不大,边界也还是清楚的,咱们把方案与影响面就放在这儿,动手改网络卷的正文是那边自己的活,咱们不抢。

## e2:syskit 的骨架,四个头一份 CMakeLists

方案定了,库的骨架就是它的落地。e2 的 syskit 是一个 INTERFACE 库(不编译出自己的产物、只把头文件路径与编译要求转发给链接方),接口就四个头:error.hpp 收编的是 errno_code 与 last_error_code,fd.hpp 收编的是 unique_fd,handle.hpp 收编的是 unique_handle,call.hpp 收编的是 sys_call 与 check_win32。每个头的注释逐个标注了收编自哪一篇,搬家走的不是重设计的路,变的只有住处与名字。mapped_region 伺候的只有 mmap 这一系,完整工程的 fs 模块会去接它,这副骨架的头四个里暂时没有它的位置。CMakeLists 咱们全文贴给您:

```cmake
# e2_syskit —— syskit 骨架的最小可编译版(篇2 e2)
# INTERFACE 库(纯头文件)+ 冒烟测试;双平台同一份 CMakeLists,零平台分支,
# 平台差异全收在头文件的 #ifdef 里——这正是本篇的主张:分支退到边界。
cmake_minimum_required(VERSION 3.16)
project(syskit_skeleton CXX)

add_library(syskit INTERFACE)
target_include_directories(syskit INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(syskit INTERFACE cxx_std_23)

enable_testing()
add_executable(smoke tests/smoke.cpp)
target_link_libraries(smoke PRIVATE syskit)
add_test(NAME smoke COMMAND smoke)
```

您从头到尾扫一遍,连一个 if 的影子都没有。双平台共用的是同一份 CMakeLists,差异全都收在了头文件里:fd.hpp 整体包在 `#ifndef _WIN32` 的守卫里,handle.hpp 反向地包,call.hpp 里两侧各住各的分支。这正是上一篇的主张在构建层的落地:[平台抽象层设计](04-platform-abstraction.md)把 #ifdef 从函数体赶到了类型边界,咱们这边再赶一步,把它赶进头文件的粒度,configure 脚本自己对平台就无感了。

```cpp
// include/syskit/error.hpp(节选)——两侧同名的别名
#ifdef _WIN32
inline std::error_code last_error_code() noexcept
{
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}
#else
inline std::error_code errno_code() noexcept
{
    return std::error_code{errno, std::generic_category()};
}
#endif

// 用户代码只写 syskit::last_error(),平台差异收进实现
#ifdef _WIN32
inline std::error_code last_error() noexcept { return last_error_code(); }
#else
inline std::error_code last_error() noexcept { return errno_code(); }
#endif
```

error.hpp 里值得看的是那个两侧同名的 last_error():POSIX 侧转的是 errno_code,Windows 侧转的是 last_error_code,用户代码认的名字只有一个。Windows 那半边还有个实用的小细节:上一篇共用源文件时记过的同款,MSYS2 的 libstdc++ 在 os_defines.h 里,已经替咱们预定义了 NOMINMAX,所以这里的头文件给 NOMINMAX 包了 #ifndef 守卫,两侧共用源文件时的安静也就是它换来的。

还有一处实现层的细节值得咱们记下:Windows 的 GetCurrentProcess() 交回的是伪句柄、值恒为 -1,它不指向真实的内核对象,值又恰好与 unique_handle 的哨兵值重合。装进去之后 operator bool 交回的是 false,析构走的也是空操作,事是不出的,可意义也是没有的。smoke 的第 4 组断言碰到了它,存档里的处理是只测包装本身的换手语义,注释里把理由也写明了:伪句柄本来就不该装进 RAII,判别的责任留在装进之前的调用方。

测试的口径也交代一下:smoke 不引测试框架,CHECK 宏把失败记了数,覆盖的是四组断言:错误路径、往返读写,加 system_error 出口与 move/swap 的换手。判的是收编有没有保住原有语义,而不是覆盖率。判据里有一处埋着语义的分叉,咱们看代码:

```cpp
// tests/smoke.cpp(节选)——异常出口的断言
try {
#ifdef _WIN32
    syskit::check_win32("CreateFileA", ::CreateFileA,
                        "syskit_no_such_dir/nope.txt", GENERIC_READ,
                        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    syskit::sys_call("open", ::open, "syskit_no_such_file.txt", O_RDONLY);
#endif
} catch (const std::system_error& e) {
    // 带目录前缀的缺失路径:Linux 统一 ENOENT(2);
    // Windows 按路径状态细分——目录不存在是 3,目录在而文件缺才是 2
    CHECK(e.code().value() == 2 || e.code().value() == 3);
}
```

咱们两侧各编各跑,输出咱们全量贴上来:

```text
$ ./build-linux/smoke
[smoke] platform=Linux
  [1] missing file: value=2 message="No such file or directory"
  [2] roundtrip: len=20 match=yes
  [3] system_error: value=2 what-prefix ok
  [4] move/swap semantics: ok
[smoke] ALL PASS

$ ./build-win/smoke.exe
[smoke] platform=Windows
  [1] missing file: value=2 message="ϵͳ�Ҳ���ָ�����ļ���"
  [2] roundtrip: len=20 match=yes
  [3] system_error: value=3 what-prefix ok
  [4] move/swap semantics: ok
[smoke] ALL PASS
```

Windows 侧的输出里有两处要停下来看。头一处是 [1] 的 message:那串乱码并不代表文件坏了,它是 system_category().message() 在 MinGW 上交回的 ANSI 代码页字节,GBK 的“系统找不到指定的文件。”被按字节直读就成了这样,思维基石 02 演过的是同款,您要 UTF-8 文本,正路是自定义的 category。

第二处是 [3] 的值:3,而不是 2。“打开带目录前缀的缺失路径”的同一个动作,Linux 统一给的是 ENOENT(2)。Windows 侧按路径状态做了细分:目录不存在给的是 3(ERROR_PATH_NOT_FOUND),目录在而文件缺才是 2(ERROR_FILE_NOT_FOUND)。所以 smoke 的断言收 2 或 3,您别把它当放宽,它收下的是两侧语义的真实形状。跨平台库的错误断言写死了单侧的形状,那才是真的写错。

## e3:CMake 平台分支的三写法

骨架有了,构建侧的问题跟着冒出来:真给一个库写 CMake,平台分支的写法有三种:if(WIN32)、generator expression、toolchain 文件,生效的时机各不相同,选错时机的代价还不直观。e3 给三写法配了六个场景,日志逐条都记了档,咱们一场一场看。

```cmake
# 写法一:if(WIN32)/if(UNIX),configure 阶段生效
add_executable(demo main.cpp)
if(WIN32)
    target_compile_definitions(demo PRIVATE SYSKIT_PLATFORM_WIN32=1)
elseif(UNIX)
    target_compile_definitions(demo PRIVATE SYSKIT_PLATFORM_POSIX=1)
endif()
```

写法一信的是 CMAKE_SYSTEM_NAME 这个平台身份。场景一走的是正常路:Linux 原生配置,UNIX 的分支选中,demo 跑起来报的是 posix branch selected。场景二就出事了:咱们裸拿 `CXX=g++.exe` 去覆盖编译器,CMake 照样把 CMAKE_SYSTEM_NAME 认成了 Linux,if(UNIX) 于是把 POSIX 的分支选中,SYSKIT_PLATFORM_POSIX 的宏进了编译命令、main.cpp 依着宏去包含 sys/epoll.h,而 g++.exe 手里没有这个头,构建当场就失败了:

```text
# 场景 2:裸覆盖 g++.exe + make(if 写法被平台身份骗走)
-- [if-variant] branch = UNIX  (CMAKE_SYSTEM_NAME=Linux)
/home/charliechen/sysprog_ch07_scratch/e3_cmake_branches/variant_if/main.cpp:7:12:
    fatal error: sys/epoll.h: No such file or directory
make[2]: *** [CMakeFiles/demo.dir/build.make:79: ...] Error 1
```

骗局成立的原因在两条线的分叉上:平台身份跟的是配置所在的系统,编译能力跟的是编译器。同一次配错的配置里,CMAKE_SYSTEM_NAME、WIN32、UNIX 全错了,而 check_include_file 的探测全对,HAVE_WINDOWS_H 老老实实地给 1,因为 try_compile 用的是真编译器。[平台抽象层设计](04-platform-abstraction.md)的检测三路把身份与能力两线量得很全,咱们这里只需要认下推论:if 认的是身份而不是能力,身份认错了它就跟着错,而且要拖到编译期才现形。

写法二走的是 generator expression,展开的时机在生成期。它有个容易误判的时点:configure 阶段您拿 message 打印它,看到的只能是原文,`$<PLATFORM_ID:Windows>` 一串字符原样地躺在日志里,此时的它还没展开。展开的证据要到 ninja -v 抓的编译命令行上找:toolchain 配对的构建里给的是 `-DGENEX_SAYS_WINDOWS=1`,Linux 原生的构建给的是 `-DGENEX_SAYS_LINUX=1`,两侧各自只有自己的宏。坏消息在于它问的 PLATFORM_ID 还是 CMAKE_SYSTEM_NAME,咱们裸覆盖时它同样被骗,场景四记下的构建失败数与 if 写法一样是 1。

写法三 toolchain 文件才是配对的正解:把“我在给 Windows 交叉”的声明放在 project() 之前,CMAKE_SYSTEM_NAME 就从探测出来的身份变成了声明的目标。本机的场景是半交叉:WSL 的 cmake 调 MSYS2 的 g++.exe,这样的链路上 toolchain 文件是唯一配得对的方法:

```cmake
# e3_cmake_branches/mingw.toolchain.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CXX_COMPILER /mnt/c/msys64/ucrt64/bin/g++.exe)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)  # try_compile 不必链接出可执行文件
```

场景六是半交叉链路上的独家实录,值得原样留给您:toolchain 配对之后首次构建是过的,可咱们 touch 一下源文件做增量重编,make 报了 multiple target patterns。原因读了两遍才敢确认:make 把编译器路径里的 `C:` 当成了目标分隔符。解法是咱们换 Ninja 生成器,增量重编立刻就正常了。还有一条副产品:interop 链路出来的产物,咱们每次重新链接都得补一次 chmod +x。您要在同样的链路上搭构建,这两件事的时间都省不下来。

```text
# 场景 6:make 生成器 + C: 路径编译器 + 增量重编的失败现场
CMakeFiles/demo.dir/compiler_depend.make:4: *** multiple target patterns.  Stop.
# 换 Ninja 之后,同一份工程增量重编正常,产物每次重链要补 chmod +x
[2/2] Linking CXX executable demo.exe
win32 branch selected (windows.h included)
```

## 基准也要按平台换手

咱们收卷之前,补一条跟测量有关的纪律,材料来自上一篇的 e4。那组实验要拿真系统调用当慢档的基准,Windows 侧差点选了 getpid:实测它只有 1.3ns,跟一次普通的函数调用打平,而总纲给过的量级是一次系统调用一两百 ns,这个对不上值得咱们起疑。疑点是从 PEB(Process Environment Block、Windows 给每个进程备在用户态的信息块)读缓存的 pid,官方的文档没有明说这个行为。真基准得换成必进内核的 GetProcessHandleCount:157ns,跟 Linux 侧 getpid 的 136 到 144ns 同量级。[总纲](../00-overview.md)讲过 glibc 2.3.4 到 2.24 缓存 getpid 的历史,Windows 侧是同型的现场,而且至今如此。跨平台抽象收得了 API 的形状,测量的基准还是得逐平台换手,不然量出来的是漂亮的假零。

## 收卷

咱们这一卷,到这儿就走完了。回头看起步的地方,[总纲](../00-overview.md)交给咱们的家底其实很薄:用户态与内核的一道边界,一次系统调用一两百 ns 的量级,POSIX 与 Win32 两大阵营的哲学对照。后来的每一站,都在往起步的家底上加有数字背书的东西。

思维基石把 fd、句柄、内存映射装进了同一副 move-only 的骨架,又把 errno 的线程局部槽位、读取窗口,连同 EINTR 的归宿量成了数字。Linux 侧咱们走过六站:文件 I/O 陪 fd 走完了它的一生,mmap 把页映射进地址空间,页缓存量了脏页与 fsync 的时序,文件系统、文件锁与 inotify 补完了 fd 周边的世界。内存站把 /proc/pid/maps 的全图摊开在咱们面前,共享内存与对齐大页咱们都亲手摸过。进程站拿 COW 实证了 fork,IPC 的七条通道与信号的消费链也各有了归宿。多路复用站翻了 1024 上限的案,时间站量出 vDSO 的十倍差,终端站在自建的 pty 台架上复刻了 script(1)。Windows 侧的收获也各不相同:文件 I/O 从 CreateFileW 与文件映射起步,VirtualAlloc 的两段式与分配粒度给了内存站骨架,CreateProcessW 的死法清理配上 Job 对象管住了进程,OVERLAPPED 一路走到 IOCP 的完成队列,控制台那边把字符网格与 VT 序列开关前后的读回都对了一遍。跨平台的收尾,03 把四个后端收进了一张矩阵,04 把检测、桥接与分派的成本量完,本篇把工具收进了库的骨架。

syskit 的去向也交代清楚:完整工程是 io、fs、process、ipc、signal、timer、tty、event 八个模块,列为独立演进的后续工程,事件循环库走的也是同一条路,本篇交付的是骨架与决策,event 的槽位留了衔接的口。您想接着长,四个头的骨架是现成的起点,咱们前面摆的对照表,就是您将来合自家近亲时的作业范本。

谢谢您陪这一卷走到最后一个字节。系统编程的功夫,是跟两个内核反复打交道打出来的。咱们一路也看着 API 旧掉、工具换代,不过边界感跟“真的吗,跑跑看”的习惯,您带在身上就好。祝您编译零警告、测量不翻车,漏掉的 fd 一个都没有。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="CMAKE_TOOLCHAIN_FILE"
    publisher="CMake Documentation"
    url="https://cmake.org/cmake/help/latest/variable/CMAKE_TOOLCHAIN_FILE.html"
  />
  <ReferenceItem
    :id="2"
    title="Generator Expressions"
    publisher="CMake Manual"
    url="https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html"
  />
  <ReferenceItem
    :id="3"
    title="CMAKE_SYSTEM_NAME"
    publisher="CMake Documentation"
    url="https://cmake.org/cmake/help/latest/variable/CMAKE_SYSTEM_NAME.html"
  />
  <ReferenceItem
    :id="4"
    title="std::error_code"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/error_code"
  />
  <ReferenceItem
    :id="5"
    title="std::expected"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/utility/expected"
  />
  <ReferenceItem
    :id="6"
    title="GetProcessHandleCount function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount"
  />
</ReferenceCard>
