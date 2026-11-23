---
title: "Design of the Platform Abstraction Layer: From #ifdef to Concepts"
description: "The same code must live on both Linux and Windows platforms. How to manage this difference is the key to making the abstraction layer stand firm. This article uses four experiments to place #ifdef and concepts in the same comparison. Predefined macros answer the question of which target is being compiled for (__GLIBC__ and _UCRT distinguish between the two C libraries of the same GCC), __has_include checks for header files but not capabilities (liburing.h only indicates that the user-space library is installed), and CMake's platform identity and compilation capabilities diverge when using CXX=g++.exe (CMAKE_SYSTEM_NAME is recognized as Linux, while try_compile's capability detection is fully correct). The dead branch experiment plants three errors into the Windows branch cut by #ifdef, resulting in zero warnings on Linux and six errors fully exposed on Windows, directly proving that the cut branch receives no diagnostics. The four-line ByteSource concept pushes #ifdef back to a single using statement for selecting the backend. The negative example missing read_some is intercepted before instantiation by both sides of GCC 16. Native_handle uses intptr_t to uniformly carry fd and HANDLE. On the Windows side, _open_osfhandle truly bridges CRT's fd, _read reads 26 bytes, and _get_osfhandle restores equality, fulfilling the promise made in the previous article. The virtual table's dispatch cost is best at 0.94ns, matching regular calls (GCC 16's speculative devirtualization assembly in the best case), worst at 5.23ns, accounting for 1%-4% of the 136-157ns system call anchor. Dispatch cost should not be the main factor in selection."
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 28
prerequisites:
  - "Cross-Platform Asynchronous I/O Abstraction"
  - "RAII Paradigm for OS Resources: fd, HANDLE, and the Same Skeleton"
  - "Error Handling Paradigm: From errno to expected"
related:
  - "Cross-Platform Asynchronous I/O Abstraction"
  - "POSIX File I/O: open/read/write and the Life of fd"
  - "Win32 File I/O: Handles, CreateFileW, and Synchronous Read/Write"
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
  title: Being Polished
  detail: This batch has just been written and is currently being proofread and refined. The expression and test results may change at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/cross-platform/04-platform-abstraction.md
  source_hash: c3e091390b50849976e42280b4c7dd8154522e283c6101d21825fc1099c67639
  translated_at: '2026-11-22T14:20:02+00:00'
  engine: qwen3:14b
  token_count: 17326
---
# Design of the Platform Abstraction Layer: From #ifdef to Concepts

[Overview](../00-overview.md) has laid out the full picture, and both sides have concluded their respective API discussions, leaving only this chapter to wrap up the work. [The previous article](03-cross-async-io.md) left two sentences for the handover. One is a polite one: the five-line AsyncBackend concept defines the shape of the completion-style asynchronous backend on the type level, allowing the same user code to run on both kernels. The other is more honest: request.target uses uintptr_t to roughly carry fd and HANDLE, promising to be resolved in this chapter. This article is here to fulfill that promise, and the real question to answer is only one: how to manage the differences between two platforms for the same code, and how to make the abstraction layer stand firm?

The most convenient move for predecessors to manage differences is #ifdef. We scatter it into function bodies, leaving two worlds in each function. The other path we are comparing is to place the differences at the boundary of types, allowing C++20 concepts to constrain the shape at compile time. What are the boundary behaviors of each path, and how much do the dispatch costs differ? We use four experiments to clarify: detection, dispatch, carrying, and cost, each taking one group.

The numbering of the experiments and the location of the products are also clearly explained to avoid you flipping the wrong files. Our four experiments are numbered e1 to e4, following the directory structure, and are unrelated to the e series in the previous article. You just need to recognize the directory: all the code, verbatim outputs, and assembly excerpts are stored in the repository's `code/volumn_codes/vol8/systems-programming/cross-platform/04-abstraction-design/` directory, and all files starting with e are from this article. The口径 of the output blocks is also set: the program outputs and compilation diagnostics referenced in the main text are marked as full or excerpts, and you should refer to the archives when comparing.

The three boundaries are also explained in advance to avoid you waiting for things you won't see. The previous article's AsyncBackend is the introduction, explaining how to use concepts to constrain an asynchronous backend and what the diagnostics of the negative example look like. We won't repeat that here, but this article expands the same idea to more common resources such as byte sources and handles. We only borrow the interface shapes of Asio and libuv: how io_context arranges the selection of the backend and type checking as a hybrid style, and how libuv wraps the handles of each platform into a unified shell. The internal implementation is not expanded. The tools themselves (unique_fd, unique_handle, sys_call, etc.) have their only definitions in the two articles of the RAII paradigm and error handling paradigm in the thinking section at the beginning of the volume. The textbook chapters on RAII and error boxing are not repeated again; collecting the scattered parts into a library is the work of the next article.

We will explain the environment口径 separately for both sides, and the numbers after are for reference. On the Linux side, it comes from the author's WSL2, with the kernel running on 6.18.33.2, and g++ is 16.2.1. The compilation command is `g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`, and the number of warnings is zero. On the Windows side, it comes from the author's Win11 26200, using MSYS2 UCRT64's g++ 16.1.0, and we invoke it from WSL through interop. The compilation command is `/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra`, and the number of warnings is also zero, with the only exception being the -O0 version of e4, which is the unoptimized version. CMake uses 4.4.3 on the WSL side. All outputs are captured from the same round on 2026-10-05.

## Three Detection Paths, Each Answering Its Own Question

The first thing to do when starting the abstraction layer is to figure out where we are and what we have in our hands. e1 sets up three detection paths: the predefined macros of the compiler, the __has_include header file probe, and the configuration-time detection by CMake. It sounds like three ways of saying the same thing.

The first path is the predefined macros. We wrote a probe `e1_platform_probe.cpp` that is the same on both sides, listing ten macros in a table, and the outputs from both sides are displayed side by side:

| Macro | Linux Side | Windows Side | What It Declares |
| --- | --- | --- | --- |
| `__linux`, `__linux__` | DEFINED | absent | Target platform is Linux |
| `_WIN32`, `_WIN64` | absent | DEFINED | Target is Windows, Win64 is only 64-bit |
| __GLIBC__ | DEFINED | absent | C library is glibc |
| _UCRT | absent | DEFINED | C library is UCRT (MSYS2 UCRT64) |
| __MINGW64__ | absent | DEFINED | MinGW-w64 toolchain |
| __CYGWIN__ | absent | absent | Cygwin compatibility layer, not present on either side |
| __APPLE__, __FreeBSD__ | absent | absent | For comparison, macOS and BSD are not present |

The most worth your attention in the table is the pair of macros for the C library. We are actually using the same GCC on both sides, with the difference only in the C library: the Linux side is linked with glibc, and the macro is __GLIBC__; the Windows side is linked with UCRT in MSYS2 UCRT64, and the macro is _UCRT. Therefore, the predefined macros answer the question precisely: the compiler is generating code for which target, and even the flavor of the C library in the target is counted. The question it cannot answer is equally straightforward: whether the local kernel has a certain mechanism, how many processors there are, these are things we can only know when the program runs. The probe ends by adding this layer: Linux uses uname to get `sysname=Linux`, `machine=x86_64`, and Windows uses GetSystemInfo to get `processor_architecture=9`, `number_of_processors=16`. Macros can never give these.

The second path brings in __has_include. It answers for us in the preprocessor phase, "Is this header in the include path?" We use five headers as probes, and the answers from both sides are complementary: on the Linux side, `sys/epoll.h`, `liburing.h`, and `sys/inotify.h` are present, while `sys/event.h` for kqueue and `windows.h` are absent. On the Windows side, only `windows.h` is detected, and the other four are not present. The output itself is quite plain, but what we really need to guard against is reading the presence of the header as the ability itself. `liburing.h` is detected, which only indicates that the user-space auxiliary library is installed, it is an independently installed library, and it does not care about the kernel's support for the io_uring mechanism. The header is an indirect criterion for ability: if the header is present, the implementation is likely there, but version and kernel details are not checked.

The third path is CMake, which knows the most information during configuration, and also has the most strange errors. In the `e1_cmake_probe` project, we let CMake print the platform identity variables, and run four `check_include_file_cxx` probes. The native configuration on Linux is normal: CMAKE_SYSTEM_NAME is Linux, UNIX is 1, and the four probes give honest answers: the headers for epoll and liburing are present, while the headers for kqueue and windows.h are not, matching the answers from the header path.

The bad scene is deliberately created by the author. The configuration command is changed to a bare `CXX=/mnt/c/msys64/ucrt64/bin/g++.exe`, and nothing else is changed. CMake still answers the identity based on the host information:

```text
e1_cmake_probe/logs/cmake_probe_scenes.txt 场景二(节选):
-- [probe] CMAKE_SYSTEM_NAME = Linux
-- [probe] CMAKE_CXX_COMPILER = /mnt/c/msys64/ucrt64/bin/g++.exe
-- [probe] WIN32= UNIX=1 MSVC= MINGW= MSYS= CYGWIN=
```

The identity is all wrong. The target files generated by g++.exe are clearly for Windows, but CMAKE_SYSTEM_NAME still answers Linux, WIN32 is empty, and UNIX is 1. Imagine if CMakeLists had written `if(UNIX)` to add POSIX source files to the target, it would have inserted the code with `sys/epoll.h` directly into g++.exe, and we would have immediately received a build failure. This failure is recorded in the six-scenario log of the next article. However, in the same scene, the ability detection is correct: the internal `check_include_file` is a try_compile, using the real compiler, and the answer returned is that windows.h is present and epoll is not, perfectly matching the real target of g++.exe. The program's output goes through config.h, reading exactly this set:

```text
e1_cmake_probe/logs/cmake_probe_scenes.txt 场景二运行段(全量):
== route 3: CMake configure-time results (via config.h) ==
  HAVE_SYS_EPOLL_H=0 HAVE_SYS_EVENT_H=0 HAVE_LIBURING_H=0 HAVE_WINDOWS_H=1
```

Therefore, the platform identity answer follows the host, and the ability detection answer follows the compiler in your hands. At CMake, they are two separate lines. The configuration on both sides is exactly the same source, and you can't feel the difference, but when we call Windows' compiler in WSL, it becomes a half cross-compile, and the answers on both sides are separated. The fix is to change the source of the target from detection to declaration, writing the action in the toolchain file, and CMAKE_SYSTEM_NAME becomes a declared value instead of a detected value. The three ways of writing and the six-scenario logs are left for the next article, and this article only needs to record the branching mechanism.

There is also a rework record left in the detection path, and the author does not intend to hide it. The probe initially wanted to use a single source file on both sides, including `sys/utsname.h` in the Linux branch, but it failed when compiled on the Windows side: the header file is absent on Windows, and it couldn't compile directly. The fix is to分流 the implementation of retrieving information at runtime by platform, leaving only three lines of #ifdef:

```cpp
#ifdef _WIN32
    SYSTEM_INFO si {};
    ::GetSystemInfo(&si);
#else
    struct utsname u {};
    ::uname(&u);
#endif
```

This is exactly our stance on #ifdef. It分流s the implementation along the platform boundary, which is where it should be. If something goes wrong, it's the usage that puts it into the business logic, not these few lines of preprocessing itself.

## The Same Function, Two Worlds

After laying out the detection paths, we look at the dispatch. e2's first scene is the most common way of writing, with a function that uses #ifdef to leave two worlds:

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

Data preparation goes through std::ofstream, and both sides run the same code, reading through their respective native APIs. We compile each side separately and paste the outputs side by side:

```text
Linux 侧(e2_ifdef_linux.out,全量):
[ifdef] backend=POSIX err=0 len=27 match=yes
[ifdef] missing-file err=2 len=0 (POSIX ENOENT=2 / Win32 ERROR_FILE_NOT_FOUND=2)

Windows 侧(e2_ifdef_windows.out,全量):
[ifdef] backend=Win32  err=0 len=27 match=yes
[ifdef] missing-file err=2 len=0 (POSIX ENOENT=2 / Win32 ERROR_FILE_NOT_FOUND=2)
```

The content read back is all correct, and the error value for the missing file is 2 on both sides. The same 2 is purely coincidental. We need to clarify: POSIX's ENOENT is 2, and Win32's ERROR_FILE_NOT_FOUND is also 2. The two numbering systems are unrelated. If you use int to store the error value, the two sides happen to match, but once you change to using strings for the message, it's immediately clear that they are different. Therefore, the unification of the error model becomes another hard battle. The pieces are given in the thinking section's [error handling paradigm](../thinking/02-error-paradigm.md), and this battle is directly fought when we collect the syskit in the next article. In this article's skeleton, the error is only passed through the number as is.

> There is also a detail when using a shared source file on both sides. The three lines of guards before windows.h are not decoration. MSYS2's libstdc++ has already defined NOMINMAX in os_defines.h, so if you manually define it again, -Wall will give a redefined warning. Therefore, it is wrapped in a #ifndef. When using a shared source file on both sides, such small differences always pop up in unexpected places. Once the guards are written, both sides are quiet.

## Dead Branch: Three Errors, This Platform Says Nothing

The first scene ran through, only proving that the side currently being compiled is correct. In the second scene, we do a more brutal experiment, using the same skeleton, but planting three errors in the Windows branch.

```cpp
// e2_ifdef_dead_branch.cpp(节选,注释是笔者种的错,原文如此)
DWORDD got = 0;                  // 错误 2:类型拼错,应为 DWORD
if (!ReaddFile(h, buf, sizeof buf, &got, nullptr)) {   // 错误 1:函数拼错
    err_out = static_cast<int>(::GetLastError());
    break;
}
::CloseHandle(h, 2);             // 错误 3:参数个数错
```

We hand it over to Linux's g++, and the compilation gives zero warnings and zero errors. The output when running is as follows:

```text
Linux 侧(e2_dead_linux.out,全量):
[dead-branch] err=0 len=23 match=yes -- 编译时死分支里的三处错误,本平台一声不吭
```

It compiled, and it ran through confidently, reading back 23 bytes without any difference. You might be wondering, where are the three errors? The preprocessor on Linux cuts off the entire branch under #ifdef _WIN32, and the code that is cut off is not compiled, not even to the lexical analysis stage. The misspelled function name and wrong parameters don't even get a chance to be seen. The same file handed over to Windows's g++ gives six errors:

```text
Windows 侧(e2_dead_windows.err,节选,要害四条):
../e2_ifdef_dead_branch.cpp:42:5: error: 'DWORDD' was not declared in this scope; did you mean 'DWORD'?
../e2_ifdef_dead_branch.cpp:44:14: error: 'ReaddFile' was not declared in this scope; did you mean 'ReadFile'?
../e2_ifdef_dead_branch.cpp:48:13: error: 'got' was not declared in this scope
../e2_ifdef_dead_branch.cpp:51:18: error: too many arguments to function 'WINBOOL CloseHandle(HANDLE)'
```

All three of the errors we planted are fully exposed, and the failure of DWORDD also causes the got to be missing the declaration. The diagnostics have grown from three to six, and the compiler has also prepared the did you mean suggestions. The direct evidence of zero diagnostics in the cut branch is presented in front of us. Errors in branches that are not compiled can sleep until someone comes. That someone might be a colleague doing cross-compilation, or it might be you starting work on a Windows machine. When the project is small, you can still say to compile both sides, but when the project grows, it gradually becomes a prayer, and prayers never participate in compilation.

## Four-Line Concept, Pushing #ifdef Back to One Place for Selecting the Backend

The third scene changes the way of handling the same thing. We declare the public shape using a concept, with only four lines of code, containing only one constraint:

```cpp
// e2_concept_reader.cpp(节选)
template <class S>
concept ByteSource = requires(S& s, void* buf, std::size_t n) {
    { s.read_some(buf, n) } -> std::same_as<std::size_t>;   // 读到多少交回多少,0=到尾
};
```

Next, two backends make their appearance. We pair them with a memory source MemSource that is common to both sides, and PosixFdSource and Win32HandleSource each implement read_some, with no #ifdef inside their implementations. The platform selection is consolidated into one using:

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

We only write one piece of user code: all backends use the same constrained template drain.

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

We paste the outputs from both sides side by side, with the backends' column only differing by one name:

```text
Linux 侧(e2_concept_linux.out,全量):
[concept] backends={Mem,POSIX} mem_len=28 nat_len=28 match=yes/yes

Windows 侧(e2_concept_windows.out,全量):
[concept] backends={Mem,Win32} mem_len=28 nat_len=28 match=yes/yes
```

There are three places in the third scene that are worth your attention. The first is the two lines of static_assert: the backend is checked the moment it is instantiated, and the assertion puts this check in the open. On Linux, NativeSource is PosixFdSource, and it is selected, so it can't escape. MemSource is compiled on both sides, and the two lines of assertions pass on both sides. The second place is drain itself. The writing at the call site is no different from runtime polymorphism, but the dispatch happens at compile time, with each backend instantiating its own version. Inlining is feasible, and virtual tables are not needed, which is the silent polymorphism under the constraint. The third place is the most significant: #ifdef has retreated from the function body and now only lives at one boundary for selecting who. The business code of drain is only one piece, and both sides run with zero branches. The differences in the platform are compressed into the implementation of the backend itself, and each backend, when compiled on its own platform, naturally checks itself with its own assertion and caller. Like in the second scene, the business layer no longer has errors left in dead branches. Asio's io_context is a well-known example of the same approach, with the backend as a service that is changeable at runtime, but type checking still remains at compile time, mixing soft and hard use. We borrow this shape.

Having only positive examples is not enough. In the fourth scene, we pair it with a negative example. It only provides read_fixed, and read_some is missing:

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

The file lists two checks. `static_assert(!ByteSource<ShortSource>)` compiles, and the concept's answer is clear, truthfully stating that it is not satisfied. Passing a ShortSource instance (called shorty in the archive) to drain in one line is definitely not compilable. We use `-c` on both sides to compile without linking, aiming for the diagnostics themselves. Both sides of GCC 16 give the same content:

```text
e2_concept_neg_linux.err(节选):
error: no matching function for call to 'drain(ShortSource&, int)'
  • candidate 1: 'template<class S>  requires  ByteSource<S> std::string drain(S&, std::size_t)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • the required expression 's.read_some(buf, n)' is invalid
```

The diagnostics name the missing parts, even bringing out the original text from the concept's one line. The diagnostic content on both sides is consistent, differing only in the quotation marks: the quotes in the Linux archive are curved, while those on the Windows side are straight. The excerpt above is typeset with straight quotes. We place it on the same table as the second scene, and the difference becomes clear: the errors in the dead branch receive no diagnostics, and the exposure timing depends on whether the code is brought to that platform. The interception point of the concept is definite, and the constraint gives a ruling before instantiation, naming which item is missing and which line of the concept it is missing. We cited one example of the negative case of AsyncBackend in the previous article, and this article's resource has changed, but the behavior shape is exactly the same, which is the extension of the same idea to more resources.

## Unified Carrying of Handles: intptr_t and the Bridge to CRT

After dispatching, the remaining task is carrying. In the previous article, request.target used uintptr_t to roughly carry the handle, and the author said it would be resolved here. We now fulfill that promise. In e3, native_handle uses a single intptr_t for unified carrying, with a pair of inports and outports on each side: from receives the native handle, and the restoration is handed over to as_fd and as_handle. The width on both sides is sizeof int=4, intptr_t=8, void*=8, and Windows side adds one line: HANDLE=8.

Our round trip on the Linux side is clean:

```text
Linux 侧(e3_linux.out,全量):
sizeof: int=4 intptr_t=8 void*=8
raw fd            = 3
roundtrip fd      = 3  identical=yes
via void*         = 3  identical=yes
read(roundtripped)= 19 bytes: 'bridge via intptr_t'
```

fd is a 4-byte int, and when placed into an 8-byte intptr_t, it is done with sign extension. For non-negative fd, the high 32 bits are all zero, and truncating back to int is lossless. You can see that the intermediate step of `void*` is also clean: converting an integer directly to `void*` is compilable, but the standard only guarantees lossless round-trip for sufficiently wide integers like intptr_t. Therefore, the correct path for int is through an intptr_t intermediary, and the restored fd is read back with 19 bytes, proving the channel is real.

The real bridge is waiting for us on the Windows side:

```text
Windows 侧(e3_windows.out,全量):
sizeof: int=4 intptr_t=8 void*=8 HANDLE=8
raw HANDLE        = 0xb0
roundtrip HANDLE  = 0xb0  identical=yes
_open_osfhandle   -> crt fd = 3 (ok)
_read(crt fd)     -> 26 bytes: 'bridge via _open_osfhandle'
_get_osfhandle    -> 0xb0  identical=yes
```

The body of HANDLE is the pointer width, and there is no width issue when placing it into intptr_t. What we really need to look at is the bridge in the middle: `_open_osfhandle` gives Win32's handle a CRT (C runtime) managed fd, so that the CRT functions like `_read`, `_close` can serve it. One `_read` reads back 26 bytes, and the reverse `_get_osfhandle` restores the handle from fd, and the restored value is equal to the original value. According to Microsoft's documentation, this call transfers the handle's ownership to the CRT, and closing is done via `_close`, and the handle is also closed together. If you add another `CloseHandle`, it becomes a double close. To interoperate between the fd world and the HANDLE world, the correct path is this bridge. libuv unifies the lifecycle of handles on both sides using the "data plus unified cleanup" shape, and it doesn't let us worry about the internal details.

We have now given the rough uintptr_t from the previous article a proper replacement. Native handles enter and exit on their respective platforms, and the unified layer only recognizes intptr_t. Both sides' widths can accommodate it, and we have all tested the restoration and equality judgment, and the real read. The differences have not been abstracted away, but they have been confined to the inports and outports of from and as_*. The rest of the code now only recognizes native_handle.

## Virtual Table vs. Templates: Best Match, Worst 5 Nanoseconds

The last set of experiments answers the most frequently debated point in selection: how much slower virtual functions are than templates. In e4, we place both polymorphisms in the same program: the base class of the strategy pattern with virtual functions, and the concept-constrained template silent polymorphism, using the same shape as read_some.

We must explain the anti-cheating design of the measurement in advance. At -O2, the monomorphic scenario's virtual call is directly eliminated by the compiler, resulting in a false zero. Therefore, we put two derived classes into a pointer array, and in the loop, we alternate the pointer according to i&1, making the dynamic type unknown at compile time. All consumption functions are marked noinline, and the dispatch location is in the function body, making the whole thing unavoidable:

```cpp
// e4_vtable_vs_template.cpp(节选)
NOINLINE static std::size_t consume_virtual(VirtualSource& s, void* buf)
{
    return s.read_some(buf, 16);      // 间接分派:call 走 vtable 槽
}
```

The output from Linux side -O2, we take the first round of two rounds:

```text
Linux 侧(e4_linux_o2.out,第一轮,全量):
[A] indirect-virtual(predictable) :   0.94 ns/op  (目标交替,BTB 全命中,最好档)
[A2] indirect-virtual(shuffled)    :   5.23 ns/op  (mt19937 乱序,预测失效,最坏档)
[B] static(template)               :   0.84 ns/op  (编译期实例化,直接调用)
[C] plain function                 :   0.84 ns/op  (普通调用锚,对齐 ch00 口径)
[D] getpid anchor                  : 138.23 ns/op  (系统调用锚)
virtual overhead vs plain : best 1.12x worst 6.24x   vs syscall : best 1% worst 4%
```

The performance levels are clear at a glance. The best performance level, alternating according to the i&1 rule, is 0.94ns, matching the direct call of the template at 0.84ns and the regular call at 0.84ns. The note in the output says BTB, which refers to the branch target buffer, a small table in the processor that caches indirect jump targets. The best performance level is exactly the hit of this buffer. The note in the output says ch00, which refers to our overview, and the anchor for the regular call is established there. The mt19937 scrambled target in the out-of-order round drops to 5.23ns, and the cost of the indirect branch prediction failure is all in these few nanoseconds. The anchor for comparison is getpid at 138ns, and our intuition from the overview that a system call takes a hundred or so nanoseconds is again confirmed. The worst-case virtual call is only 4% of that. The second round's readings (5.16 and 136.14ns) are stable at the same level. The -O0 comparison in the archive has readings of 4.17 and 9.05ns, and the gap is actually smaller in the unoptimized world, indicating that these few nanoseconds are originally made by the optimizer and branch prediction together.

Why can 0.94 match? The answer is ready in the assembly. We marked consume_virtual as noinline, but GCC 16 -O2 does more than "honestly go through the virtual table" inside it:

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

We look at what the compiler does: it takes the slot from the vtable, compares the address in the slot with the addresses of the two final derived classes' functions one by one, guesses which one, and directly inlines the implementation of that one, ending with only two SSE instructions. If neither is guessed, it falls back to an indirect jump. This is the mechanism of speculative devirtualization: the hierarchy is closed, the candidates are only two, and the derived classes are marked final, so the compiler dares to bet. The probability of hitting in the hot loop is high, and the branch prediction protects another layer. The old saying that virtual functions are slow is rewritten in the context of a closed hierarchy and modern GCC: when the branch prediction hits, it matches the direct call, and when it fails, it's about five nanoseconds, and both are far less than a system call. consume_static ends with three instructions, saving even the bet.

We also look at the Windows side, and the same compiler (-O2) produces the same performance levels: the best is 1.04ns, the worst is 4.61ns, the template is 0.75ns, and the regular is 0.76ns. However, there is a strange thing on the anchor side: getpid only counts 1.32ns. This is almost equal to a regular function call, and a system call is not this price. The UCRT's getpid is suspected to be a purely user-space implementation, reading the cached pid from the PEB (process environment block, a block of information stored in user-space for each process by Windows), but the official documentation does not explicitly say this, and we can only remain doubtful. Using it as an anchor for system calls, the measured proportion becomes a false zero. Changing to GetProcessHandleCount, which must enter the kernel, reads 157.41ns, which is the same level as getpid on the Linux side (136-144ns), and the percentage returns to 1%-3%. We have discussed the history of glibc 2.3.4 to 2.24 caching getpid in the overview, and it was removed in 2.25. The same situation on the Windows side still exists today. Cross-platform abstraction even needs to change the anchor by platform, which is the additional lesson e4 gives us.

The four experiments are done, and the answer to the selection can be set straight. The dispatch gap between the virtual table and the template is best at a few tenths of a nanosecond, and worst at about five nanoseconds, which is 1%-4% of the 136-157ns system call volume. Under the abstraction layer of system programming, any real action is a system call-level cost, and the dispatch cost should not be the main factor in selection. What should be looked at is the thing e2 has already measured: the timing of error exposure, and whether it names it when it is exposed. The timing of the diagnostics and the code organization are the two worth seriously debating between #ifdef and concepts. In the next article, we will collect the scattered tools in the volume into the syskit namespace, paired with a CMake and smoke test with no platform branches, and then look at the claim that #ifdef has retreated to the type boundary, it should still be clean.

<ReferenceCard title="Reference Resources">
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
