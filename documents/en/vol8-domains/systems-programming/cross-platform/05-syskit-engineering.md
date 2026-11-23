---
title: "syskit Tool Library Engineering"
description: "vol8 The final piece of system programming: the unique_fd/sys_call/errno_code defined in the thinking foundation and the UniqueFd/SysError in the networking are two closely related sets. e1 copies both definitions into the same translation unit, detection idiom probes members, noexcept probes and standard traits are fully compared (skeleton isomorphic: 4 bytes and nothrow move, differences lie in reset with parameters, member swap and is_swappable dual true detection traps, noexcept annotations, EINTR destination, error model 40 bytes with context vs 16 bytes determinable), the unified solution is named syskit:: with snake case and error_code dual exits, the networking side impact is checked file by file (three documents with four points, code side 01-modern-socket five files plus the second copy of the lab0 scaffolding and five test references, 02 and 03 do not reference), e2 incorporates the four tools into the INTERFACE library's syskit skeleton, each of the four headers is annotated with which article it was incorporated from, the same CMakeLists file is used for both platforms without platform branches, smoke tests are compiled and run on both sides, Windows side error values 2 and 3 semantics split and message() ANSI code page bytes are recorded accurately, e3 uses if, generator expression, and toolchain file three methods for six scenarios to measure the timing of CMake platform branches, exclusive recordings of two lines of fork under bare CXX coverage where platform identity is misrecognized but compilation capability is fully correct, genex expansion during generation with ninja -v command line evidence, make treats C: as a target separator and incremental recompilation fails, before wrapping up, add one benchmark discipline, Windows side getpid timing 1.3ns suspected to read from PEB cache, real benchmark must switch to kernel's GetProcessHandleCount at 157ns"
chapter: 8
order: 5
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 27
prerequisites:
  - "Platform abstraction layer design: from #ifdef to concepts(ch07 first article)"
  - "OS resource RAII paradigm: fd, HANDLE and mapping with the same skeleton"
  - "Error handling paradigm: from errno to expected"
  - "Modern socket encapsulation: RAII, std::expected, and the C10K that can't handle per connection thread (only depends on its tool segment)"
related:
  - "Platform abstraction layer design: from #ifdef to concepts"
  - "Cross-platform asynchronous I/O abstraction"
  - "OS resource RAII paradigm: fd, HANDLE and mapping with the same skeleton"
  - "Error handling paradigm: from errno to expected"
  - "error_code: error code system and custom category"
  - "expected: value or error, C++23's new error handling paradigm"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - 工程实践
  - CMake
current_status:
  title: Being polished
  detail: This batch has just been written and is currently being read and polished, the expression and practical test口径 may change at any time
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/cross-platform/05-syskit-engineering.md
  source_hash: 50eff3aac20569d554887bb9abbb313312cde8f853b0b1078821b0d5faf9f8a8
  translated_at: '2026-11-22T14:17:42+00:00'
  engine: qwen3:14b
  token_count: 17768
---
# syskit Tool Library Engineering

We've reached the end of this volume. Finishing it doesn't mean just aligning the articles. The tools like unique_fd, sys_call, and errno_code have been used throughout various stations, but their definitions are still in the two articles of the thinking foundation. The references are all done by each article itself. To truly compile them into a library, we need to give them a formal home and an engineering that compiles on both sides. However, before we start, there's a more complicated problem waiting for us: in fact, the same tools are stored in two sets in our repository.

The 01 article in the networking volume defines UniqueFd and SysError itself, which are close relatives to the tools in this volume, with many similarities but also many differences. If you've read both the networking volume and this one, you probably have a vague sense of this. The coexistence of the two sets is in front of us, and when we compile the library, we have to answer directly: should they be merged, how to merge, and what to do with the articles and code that reference them. This article will use three experiments to handle this matter smoothly: e1 puts both sets of tools into the same translation unit for item-by-item comparison, e2 builds the skeleton of the syskit library, and e3 measures the timing of the platform branch in the build script using three methods.

The numbering of the experiments is explained at the beginning. The experiments in this article are lowercase e1 to e3, which are unrelated to the e series in [03 Cross-Platform Asynchronous I/O Abstraction](03-cross-async-io.md) and the e series in [Platform Abstraction Layer Design](04-platform-abstraction.md). When you refer to the archives, just recognize the directory. All the content in this article is collected under `code/volumn_codes/vol8/systems-programming/cross-platform/05-syskit/`. The output口径 is also mentioned briefly: the output of e1 and the smoke (smoke) output of e2 are fully referenced, and the scenario logs of e3 are excerpts. When you refer to the table, use the archives as the standard.

The口径 of the environment is separated on both sides, and the subsequent outputs are all matched with it. On the Linux side, it comes from the author's WSL2: kernel 6.18.33.2, g++ 16.2.1, cmake 4.4.3. The probe of e1 and the skeleton of e2 use std::expected, and the compilation is done with `-std=c++23`, the optimization and warning口径 is `-O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`, and the number of warnings obtained is zero. On the Windows side, it comes from the author's Win11 26200, MSYS2 UCRT64's g++ 16.1.0, which is called from WSL through interop, and cmake still uses the 4.4.3 on the WSL side, with a cross-paired toolchain file and Ninja generator. The encounter with make is recorded in scenario six of e3. All outputs are captured from the same round on October 5, 2026.

## How the Two Sets of Close Relatives Came to Be

This volume's thinking foundation has established an agreement: unique_fd, unique_handle, mapped_region, and sys_call are all defined in one place, and the subsequent stations only reference, not rewrite. The networking volume was written before the agreement was established, and its [Modern Socket Encapsulation](../../networking/01-modern-socket-wrapping.md) defined UniqueFd and SysError itself, establishing another set. When we put the two sets together, both sets of code work well, but the trouble comes from the details that have quietly forked, and the fork is not even.

The name divergence is the most obvious. In the main text of networking/01, the class name is Pascal's `UniqueFd`, but in its code file, the filename is snake-case `unique_fd.hpp`. When we look back at the preview at the end of networking/00, it's written in lowercase `unique_fd`. The same tool has grown into three different forms. You might think it's a small matter, but what we're going to do next is to unify the tool into a namespace and distribute it uniformly. If the names are not unified, each migration after unification will create ambiguity.

## e1: Comparison Probe in the Same Translation Unit

Our comparison must be honest, so we can't list the list based on impressions, but we have to ask the compiler to explain itself. e1's approach is to copy both sets of definitions into the same translation unit as they are: the net:: namespace houses the networking's UniqueFd and SysError, and the sysp:: houses the thinking foundation's unique_fd and errno_code, with the source also noted in the comments, and the semantics are moved verbatim. The probe is divided into two layers: the trait layer uses detection idiom to probe for members, and the noexcept probe probes for annotations. At the runtime level, the same ENOENT is made to go through both sets of packaging, and see what each error object gives.

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

Detection idiom probes for "existence of members", and the noexcept probe probes for "annotations". We combine both, and the interface differences are hidden. We fully paste the output, and you compare line by line:

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

At the end of the C section, there is a line of a suspended title, followed by no reading: the probe was originally intended to demonstrate the raw int of SysError compared to the errc enumeration, which is directly not compilable, and must go through a manually written mapping, which is written in the source code comments, and the output only left this line of title. We follow the full reference口径 and record it as is.

## A Detailed Comparison Table

We read the output together with the two source codes, and the relationship between the two sets of tools can be summarized into a table. At the skeleton level, the two sets are completely isomorphic: 4 bytes, nothrow move, default constructible, and the readings of section A are all the same. All the differences are in the interface and error model. We list them one by one for you:

| Dimension | net::UniqueFd and SysError | This volume's unique_fd and sys_call | Comments |
| --- | --- | --- | --- |
| Definition location | networking/01 and its code file | Thinking foundation two articles, the only definition in the entire volume | One is in the networking volume as a separate article, one has the agreement of this volume |
| Naming | Pascal UniqueFd, filename is snake-case, preview writes lowercase | Snake-case, same as the standard library | Three drifts are a ready-made motive for unification |
| Platform coverage | Single-side Linux | unique_fd and unique_handle dual-side matching | Each manages a segment, only together are complete |
| Size and move semantics | sizeof 4, nothrow move, default constructible | Measured to be exactly the same | Skeleton isomorphic, resistance to merging is not at this level |
| Reset | Only parameterless, close and empty | Overloaded with parameters, close old and take new | This volume has an extra path for on-site replacement |
| Swap | No member, general std::swap as fallback, three moves | Member directly swaps two ints, one exchange | Both sets are true for is_swappable, detection must probe members |
| noexcept annotation | get, release, and reset are not annotated | Fully annotated | No annotation equals no promise at the trait level |
| Error model | SysError, sizeof 40, context follows the error | error_code, sizeof 16, message and errc equality | Design divergence, discussed below |
| EINTR | Handwritten continue in the call loop | sys_call internal retry | Closer to the library |
| Language standard | C++23 (expected) | error_code route C++20 available | Packing and equality available in C++20 |

The most worth noting line in the table is the swap row. The net version has no swap member, but std::is_swappable still returns true, and the general version of std::swap covers it, at the cost of three real moves: one move construction, then two move assignments. The swap of the this volume version is member swapping of two ints, and it's done with one exchange. The zero move shuffle in the thinking foundation's vector experiment relies on the member swap to support it. So, if you want to detect whether a type has swap, you must probe the member rather than the trait, the trait returns true but actually guarantees nothing.

The two lines of naming and platform coverage, we read them together. The net version is active only on the Linux side, and the name divergence is only visible in the Linux code. This volume's set has been dual-platform since the thinking foundation, and unique_fd and unique_handle are written together. The snake-case name is also available in the standard libraries on both sides. After unifying into syskit::, you write the same name on both sides, and there is no second set of writing.

The line of noexcept is also worth pausing. These functions on both sides will not throw semantically, but the net version of these functions is not annotated, so the trait level does not promise, and the noexcept probe returns 0. The lack of annotation affects the attitude of generic code towards unannotated types. The handling of containers and algorithms goes for the conservative "may throw"口径. When we compile into the library, we complete the annotations that should be annotated, and the skeleton of e2 also follows the original thinking foundation to retain the full noexcept annotations, and the promise is fulfilled this way.

The reset with parameters and the EINTR destination are briefly mentioned in the table, but we have real tests on file: the "close old and take new" of the parameterized reset, the thinking foundation's strace archive has demonstrated it, and the complete evidence chain of EINTR being collected into the internal retry of sys_call is in [Error Handling Paradigm](../thinking/02-error-paradigm.md). The direction of these two differences is consistent: the set of this volume is closer to the library's form, and fewer changes are needed when it is included.

## Error Model: Context vs Equality

The only part in the table that needs us to expand separately is the error model, because it is a real design divergence between the two sets, not because someone forgot to write. The full chain of D section's comparison clearly shows the divergence: the same failure to open a non-existent file, the net-style failure report is `context="open" errno=2`, "which step went wrong" is stored in the error object. The this volume-style returns `value=2 message="No such file or directory"`, the standard text is ready, "which step" information, you have to supplement it by the call site.

The advantage of SysError is context: the string follows the error object, and it is directly readable in the expected error channel. You don't need to look for information elsewhere when you log. The cost is straightforward, the measured sizeof is 40, which contains a std::string, and once the context is long, it needs to move to the heap, and memory allocation on the failure path cannot be avoided. This is a significant weight in system programming.

The advantage of error_code is value semantics: its 16-byte size, copying is zero-cost, message() gives standard text, errc equality is measured to be true, and the Windows-side system_category can bridge, the upgrade path to custom win32_category is also demonstrated in thinking foundation 02. You look back, error_code only loses "which step", and there are two ready-made ways to supplement it: the first parameter of sys_call will go into the prefix of system_error's what, or_else records the log of the call site, both ways are demonstrated in E3 of thinking foundation 02. Both mechanisms have real test backing, so after unifying to error_code, the information volume is not really lost.

We restate the dual-exit agreement conveniently: the tool layer returns `expected<T, error_code>`, and the application layer packages the error into a system_error and throws it out, converging all the throw points in the main function. The mechanisms of error_code and expected are in vol3's [error_code](../../../vol3-standard-library/error-utils/66-error-code.md) and [expected](../../../vol3-standard-library/error-utils/64-expected.md), we only use them, and we don't repeat the lessons.

## Unified Solution and Impact Scope

The solution can now be presented, only two: naming unified to `syskit::` snake-case, error model unified to error_code dual-exit. I don't pretend this is the only solution: if your project's error reporting deeply depends on context strings, the SysError route is also valid. However, after the real tests of both sets are laid out, the harder side of equality and cross-platform bridging, I chose error_code, and the context gap is filled with the what prefix and call site log.

We have checked the impact scope file by file, and give you a complete list. On the document side, it's one sentence in networking/00's preview, the definition and use case in networking/01's main text, plus one line in lab0's prerequisite knowledge and the `include/net/unique_fd.hpp` in the scaffolding list, four points in three documents. On the code side, the references are in two areas: one is the five files (unique_fd.hpp, echo_server, echo_client, hold_clients, and README) under `code/volumn_codes/vol8/networking/01-modern-socket/`, where echo_server has six references, echo_client and hold_clients have three references in total, and README also mentions it once. The other is the scaffolding in `code/volumn_codes/vol8-labs/lab0-mini-reactor/`, the second complete copy of UniqueFd is in the include/net/unique_fd.hpp of the scaffolding, and the test file `tests/lab0_tests.cpp` has five references. We have also checked 02-epoll and 03-reactor, and neither of the two articles' main texts reference it. The action of inclusion is still small, and the boundaries are still clear, we put the solution and impact scope here, and the action of modifying the main text of the networking volume is their own work, we don't take it.

## e2: syskit's Skeleton, Four Headers and One CMakeLists

The solution is determined, and the skeleton of the library is its implementation. e2's syskit is an INTERFACE library (does not compile its own product, only forwards the header file path and compilation requirements to the linker), with four interfaces: error.hpp incorporates errno_code and last_error_code, fd.hpp incorporates unique_fd, handle.hpp incorporates unique_handle, call.hpp incorporates sys_call and check_win32. Each header's comment individually notes which article it was incorporated from, and the move is not a redesign path, only the residence and name have changed. Mapped_region only serves the mmap series, the complete fs module of the full project will take it over, and these four headers in this skeleton do not have its place yet. We paste the full CMakeLists for you:

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

You scan through it from start to finish, and there is not a single trace of an if. The same CMakeLists is shared by both platforms, and all the differences are collected in the header files: fd.hpp is entirely wrapped in the `#ifndef _WIN32` guard, handle.hpp is wrapped in the opposite direction, and call.hpp has each side's branch. This is exactly the assertion of the previous article implemented at the build level: [Platform Abstraction Layer Design](04-platform-abstraction.md) has driven #ifdef out of the function body to the type boundary, and we have driven it further, into the granularity of the header file, the configure script itself is insensitive to the platform.

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

What's worth looking at in error.hpp is that same-named last_error(): on the POSIX side, it turns into errno_code, on the Windows side, it turns into last_error_code, and the user code recognizes only one name. On the Windows side, there's also a small detail: the same one from the previous article's shared source file, MSYS2's libstdc++ in os_defines.h has already pre-defined NOMINMAX for us, so the header file here has a #ifndef guard for NOMINMAX, and the quietness when sharing source files on both sides is achieved by it.

There is also one implementation-level detail worth noting: Windows's GetCurrentProcess() returns a pseudo handle, which is always -1, it does not point to a real kernel object, and its value coincides with the sentinel value of unique_handle. After inserting it, operator bool returns false, and the destructor also performs an empty operation, no incident occurs, but the meaning is also absent. The fourth group of assertions in smoke encountered it, and the handling in the archive is to only test the handover semantics of the packaging itself, and the reason is also written in the comments: pseudo handles should not be put into RAII, the responsibility of discrimination is left to the caller before insertion.

The test口径 is also explained: smoke does not reference the test framework, the CHECK macro records the number of failures, and covers four groups of assertions: error path, round-trip read/write, system_error exit, and move/swap handover. It checks whether the inclusion has preserved the original semantics, not the coverage rate. There is one place where a semantic fork is buried in the criteria, we look at the code:

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

We compile and run on both sides, and we fully paste the output:

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

There are two places in the Windows output that need to be stopped and looked at. The first is [1]'s message: that string of garbled text does not mean the file is broken, it is the ANSI code page bytes returned by system_category().message() on MinGW, the GBK "The system cannot find the file specified." is read as bytes directly, which is the same as the one demonstrated in thinking foundation 02, you want UTF-8 text, the right way is to define a custom category.

The second is [3]'s value: 3, not 2. For the same action of "opening a missing path with a directory prefix", Linux uniformly gives ENOENT (2). On the Windows side, it has been细分 according to the path status: directory does not exist gives 3 (ERROR_PATH_NOT_FOUND), directory exists but file is missing is 2 (ERROR_FILE_NOT_FOUND). So smoke's assertion accepts 2 or 3, you don't treat it as a relaxation, it accepts the real shape of the semantics on both sides. If a cross-platform library's error assertion hardcodes the shape of one side, that's really a mistake.

## e3: Three Ways to Write CMake Platform Branches

The skeleton is there, and the build-side issues follow. Writing a real CMake library, the platform branch has three ways: if(WIN32), generator expression, toolchain file, and the timing of their effectiveness is different, and the cost of choosing the wrong timing is not intuitive. e3 provides six scenarios for the three methods, and the logs are recorded one by one, we look at them one by one.

```cmake
# 写法一:if(WIN32)/if(UNIX),configure 阶段生效
add_executable(demo main.cpp)
if(WIN32)
    target_compile_definitions(demo PRIVATE SYSKIT_PLATFORM_WIN32=1)
elseif(UNIX)
    target_compile_definitions(demo PRIVATE SYSKIT_PLATFORM_POSIX=1)
endif()
```

Method one relies on CMAKE_SYSTEM_NAME for platform identity. Scenario one takes the normal route: Linux native configuration, UNIX branch selected, demo runs and reports "posix branch selected". Scenario two is problematic: we bare `CXX=g++.exe` to cover the compiler, CMake still recognizes CMAKE_SYSTEM_NAME as Linux, if(UNIX) selects the POSIX branch, the macro SYSKIT_PLATFORM_POSIX enters the compilation command, main.cpp includes sys/epoll.h according to the macro, but g++.exe doesn't have this header, the build fails immediately:

```text
# 场景 2:裸覆盖 g++.exe + make(if 写法被平台身份骗走)
-- [if-variant] branch = UNIX  (CMAKE_SYSTEM_NAME=Linux)
/home/charliechen/sysprog_ch07_scratch/e3_cmake_branches/variant_if/main.cpp:7:12:
    fatal error: sys/epoll.h: No such file or directory
make[2]: *** [CMakeFiles/demo.dir/build.make:79: ...] Error 1
```

The reason the scam works is on two lines of fork: platform identity follows the system where the configuration is located, and compilation capability follows the compiler. In the same configuration error, CMAKE_SYSTEM_NAME, WIN32, UNIX are all wrong, but check_include_file detection is all correct, HAVE_WINDOWS_H is honestly 1, because try_compile uses the real compiler. The three-way detection in [Platform Abstraction Layer Design](04-platform-abstraction.md) fully measures the identity and capability of the two lines, we only need to accept the inference here: if recognizes identity rather than capability, identity is wrong and it follows, and it appears at compile time.

Method two uses generator expression, which expands at the generation time. It has an easy-to-misjudge timing: during the configure phase, you print it with message, you can only see the original text, the string `$<PLATFORM_ID:Windows>` is still lying in the log, and it hasn't expanded yet. The evidence of expansion has to be found in the compilation command line captured by ninja -v: the build with toolchain pairing gives `-DGENEX_SAYS_WINDOWS=1`, the native Linux build gives `-DGENEX_SAYS_LINUX=1`, each side has only its own macro. The bad news is that it still asks PLATFORM_ID, which is CMAKE_SYSTEM_NAME, and it is also deceived when we bare coverage, the number of build failures recorded in scenario four is the same as the if method, which is 1.

Method three, the toolchain file is the correct pairing: declare "I'm giving Windows cross" before project(), and CMAKE_SYSTEM_NAME changes from the detected identity to the declared target. The local scenario is semi-cross: WSL's cmake calls MSYS2's g++.exe, and the toolchain file is the only way to pair on this chain:

```cmake
# e3_cmake_branches/mingw.toolchain.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_CXX_COMPILER /mnt/c/msys64/ucrt64/bin/g++.exe)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)  # try_compile 不必链接出可执行文件
```

Scenario six is an exclusive recording on the semi-cross chain, worth leaving to you as is: after the first build with toolchain pairing is passed, we touch the source file to do incremental recompilation, and make reports multiple target patterns. The reason took two reads to confirm: make treats the compiler path's `C:` as a target separator. The solution is to switch to the Ninja generator, and incremental recompilation is immediately normal. There is also a by-product: the product from the interop chain, we have to add chmod +x every time we relink. If you want to set up the build on the same chain, these two things' time cannot be saved.

```text
# 场景 6:make 生成器 + C: 路径编译器 + 增量重编的失败现场
CMakeFiles/demo.dir/compiler_depend.make:4: *** multiple target patterns.  Stop.
# 换 Ninja 之后,同一份工程增量重编正常,产物每次重链要补 chmod +x
[2/2] Linking CXX executable demo.exe
win32 branch selected (windows.h included)
```

## Benchmark Also Needs to Be Switched by Platform

Before wrapping up, we add one discipline related to measurement, the material comes from the e4 of the previous article. That experiment uses real system calls as the slow benchmark, and Windows side almost chose getpid: the measured result is only 1.3ns, which is on par with a regular function call, but the general outline gives the level of a system call as one or two hundred ns, this discrepancy is worth our suspicion. The suspicion is reading the cache of pid from the PEB (Process Environment Block, the information block reserved by Windows for each process in user space), the official documentation does not explicitly mention this behavior. The real benchmark must be switched to the kernel's GetProcessHandleCount: 157ns, which is the same level as Linux's getpid of 136 to 144ns. The general outline in [Overview](../00-overview.md) talks about the history of glibc 2.3.4 to 2.24 caching getpid, and the Windows side is the same on-site, and it's still the case today. Cross-platform abstraction can collect the shape of the API, but the measurement benchmark still needs to be switched per platform, otherwise the measured result is a beautiful fake zero.

## Wrap-up

We've reached the end of this volume. Looking back at the starting point, the thin foundation given to us by [Overview](../00-overview.md) is actually very thin: a boundary between user space and kernel, the level of a system call as one or two hundred ns, the philosophical comparison between POSIX and Win32. Every subsequent station has added something with numerical backing to the starting foundation.

The thinking foundation has put fd, handles, and memory mapping into the same move-only skeleton, and has also measured the thread-local slot of errno, the read window, and the destination of EINTR into numbers. On the Linux side, we've gone through six stations: file I/O has walked through the life of fd, mmap has mapped pages into the address space, page cache has measured the timing of dirty pages and fsync, the file system, file locks, and inotify have completed the world around fd. The memory station has laid out the full map of /proc/pid/maps in front of us, and we've personally touched shared memory and aligned huge pages. The process station has used COW to prove fork, and the seven channels of IPC and the consumption chain of signals have each had their destinations. The multiplexing station has turned over the case of the 1024 limit, the time station has measured the tenfold difference of vDSO, and the terminal station has replicated script(1) on its self-built pty platform. The harvest on the Windows side is also different: file I/O starts with CreateFileW and file mapping, the two-stage and allocation granularity of VirtualAlloc give the memory station skeleton, the death cleanup of CreateProcessW is paired with Job objects to manage the process, OVERLAPPED goes all the way to the completion queue of IOCP, and the console side has checked the character grid and the read back before and after the VT sequence switch. The cross-platform conclusion, 03 has collected four backends into a matrix, 04 has measured the cost of detection, bridging, and dispatching, and this article has collected the tools into the library skeleton.

The direction of syskit is also clear: the complete project is eight modules: io, fs, process, ipc, signal, timer, tty, event, listed as independent subsequent projects, the event loop library also takes the same path, this article delivers the skeleton and decision, and the slot for event is left for connection. If you want to continue growing, the four-header skeleton is a ready-made starting point, and the comparison table we've laid out in front of you is your future homework template for merging your own close relatives.

Thank you for accompanying this volume to the last byte. The skills of system programming are forged through repeated interactions with two kernels. We've also seen APIs become outdated and tools evolve along the way, but the sense of boundary and the habit of "really? let's run it" are yours to carry with you. Wishing you zero warnings in compilation, no measurement failures, and no missing fds.
