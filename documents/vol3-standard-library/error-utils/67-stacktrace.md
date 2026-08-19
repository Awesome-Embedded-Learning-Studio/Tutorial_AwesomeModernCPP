---
chapter: 7
cpp_standard:
- 23
description: 讲透 std::stacktrace 怎么采集运行时调用栈、libstdc++exp 的链接坑、有/无调试符号与 strip 后的符号化差别，以及它和编译期 source_location 各自该出现在哪里
difficulty: intermediate
order: 67
platform: host
prerequisites:
  - error_code：错误码体系与自定义 category
  - expected：值或错误，C++23 的错误处理新范式
reading_time_minutes: 14
related:
  - 'source_location：编译期代码位置，__FILE__ 的类型安全替代'
tags:
  - host
  - cpp-modern
  - intermediate
  - 基础
title: 'stacktrace：C++23 终于标准化的调用栈采集'
---

# stacktrace：C++23 终于标准化的调用栈采集

写过服务端或带点规模的应用，你一定踩过这个坑：程序在某个错误分支上挂了，日志里只有一句"处理失败"，至于"谁调的谁、从哪条路径走到这里"——一概不知。排查时只能回去猜调用关系，或者临时往代码里到处塞 `__FILE__` / `__LINE__` 打点。

C++23 之前，想在运行时拿调用栈(backtrace)只能各显神通：Linux 上手撕 `backtrace()` / `backtrace_symbols()` 这套 libc 接口，Windows 上抓 `CaptureStackBackTrace` + `SymFromAddr`，跨平台干脆上 `boost::stacktrace`。这些方案各有各的坑——libc 那套不 demangle、要自己接 `abi::__cxa_demangle`；Windows 的符号引擎要单独初始化。C++23 把这件事标准化了：`<stacktrace>` 头文件，一套跨平台、类型安全的调用栈采集接口。这一篇我们就把它拆透，顺带把真实工程里一定会撞上的两个硬坑——**链接库**和**符号依赖**——一起讲清楚。

## 一句话先建立直觉

`std::stacktrace` 是一张**运行时的调用栈快照**：在程序执行的某个瞬间，把"当前这条路径上所有还没返回的函数"按调用顺序记下来，每一帧给你函数名、源文件、行号。它的典型用法就一行：

```cpp
// Standard: C++23
auto st = std::stacktrace::current();   // 在此刻拍一张栈快照
std::cout << std::to_string(st);        // 打印成 gdb 风格的多行文本
```

注意这里有个关键设计选择：`current()` 只是**采集地址**(程序计数器 PC + 帧信息)，它**不**做符号化。符号化(`description()` / `source_file()` / `source_line()`)是你在访问某个 `stacktrace_entry` 时才按需发生的。这个"采集和符号化解耦"的设计直接决定了后面要讲的性能差异——采集很便宜，符号化才贵。

## basic_stacktrace 与 stacktrace_entry：两层结构

标准库给了两个类，分工明确：

- `std::basic_stacktrace<Allocator>` —— 一个"帧的序列"，像 `vector` 一样能 `size()`、能下标访问、能遍历。`std::stacktrace` 是 `basic_stacktrace<std::allocator<stacktrace_entry>>` 的别名。
- `std::stacktrace_entry` —— 单个栈帧，代表"某个函数的一次调用"。它本身很轻量，内部就存一个程序计数器(`native_handle()`)，符号信息是查询时才现算的。

`stacktrace_entry` 的查询接口只有三个真正拿数据的成员：

```cpp
// Standard: C++23
std::string description() const;     // demangle 后的可读描述，如 "foo(int)"
std::string source_file() const;     // 源文件路径，无调试符号时为空
std::uint_least32_t source_line() const;  // 源文件行号，无调试符号时为 0
```

我们上手跑一个最小的例子，把每一帧的成员都打出来看：

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <string>

void inspect(int x) {
    auto st = std::stacktrace::current();
    std::cout << "depth = " << st.size() << '\n';
    for (std::size_t i = 0; i < st.size(); ++i) {
        const auto& e = st[i];
        std::cout << "--- entry " << i << " ---\n";
        std::cout << "  native_handle : " << e.native_handle() << '\n';
        std::cout << "  bool(e)       : " << (e ? "true" : "false") << '\n';
        std::cout << "  description   : [" << e.description() << "]\n";
        std::cout << "  source_file   : [" << e.source_file() << "]\n";
        std::cout << "  source_line   : " << e.source_line() << '\n';
    }
}

void caller_a(int v) { inspect(v); }

int main() {
    caller_a(7);
    return 0;
}
```

用 `g++ -std=c++23 -O0 -g`(本机 GCC 16.1.1)编译运行——注意末尾那个 `-lstdc++exp`，这是本篇最重要的一个坑，我们下一节专门讲它，先按它跑通：

```text
depth = 6
--- entry 0 ---
  native_handle : 109511442723472
  bool(e)       : true
  description   : [inspect(int)]
  source_file   : [/tmp/st_members.cpp]
  source_line   : 6
--- entry 1 ---
  native_handle : 109511442724282
  bool(e)       : true
  description   : [caller_a(int)]
  source_file   : [/tmp/st_members.cpp]
  source_line   : 20
--- entry 2 ---
  native_handle : 109511442724299
  bool(e)       : true
  description   : [main]
  source_file   : [/tmp/st_members.cpp]
  source_line   : 23
--- entry 3 ---
  native_handle : 123497777100608
  bool(e)       : true
  description   : []
  source_file   : []
  source_line   : 0
--- entry 4 ---
  native_handle : 123497777100920
  bool(e)       : true
  description   : [__libc_start_main]
  source_file   : []
  source_line   : 0
--- entry 5 ---
  native_handle : 109511442723204
  bool(e)       : true
  description   : [_start]
  source_file   : []
  source_line   : 0
```

几个值得留意的点。首先，栈顶(entry 0)是**正在执行 `current()` 的那个函数**本身，往下才是逐级调用者，一直到 `_start`(程序的真正入口)和 `__libc_start_main`(C 运行时)。其次，越往下越"不可知"——libc 和 `_start` 没有调试符号，所以它们的 `source_file` / `source_line` 是空的，中间还夹着一帧完全 `<unknown>` 的(entry 3，通常是 libc 内部的跳板)。这正是栈采集的真实面貌：**自己代码的帧能拿到全信息，越往运行时深处越是黑盒**，别指望每帧都完整。

## 第一个硬坑：链接库 libstdc++exp

现在回头看那个 `-lstdc++exp`。这是新手第一道坎，几乎人人中招。如果你按平时习惯直接编译：

```text
$ g++ -std=c++23 -O2 -g st_members.cpp -o st_members
/usr/bin/ld: .../stacktrace:209:(.text+0x4a):
  undefined reference to `std::__stacktrace_impl::_S_current(...)'
/usr/bin/ld: .../stacktrace:167:(.text._ZStlsRSoRKSt16stacktrace_entry+0xc1):
  undefined reference to `std::stacktrace_entry::_Info::_M_populate(unsigned long)'
collect2: error: ld returned 1 exit status
```

编译过了，链接挂了。报错说的是两个符号找不到：`_S_current`(采集栈的实现)和 `_M_populate`(符号化的实现)。原因在于 libstdc++ **没有**把 `<stacktrace>` 的实现编进默认链接的 `libstdc++.so` 里——采集和符号化涉及平台相关的底层逻辑(backtrace / dladdr / DWARF 解析)，体积不小，标准库把它拆出来单独放一个库，谁用谁链接。

::: warning 必须显式链接实验库
libstdc++ 的 `<stacktrace>` 实现住在**实验库**里，默认不链接。GCC 工具链的约定：

- **GCC 16 及以上**(本机 GCC 16.1.1 实测)：库名是 `libstdc++exp`，链接参数 `-lstdc++exp`(注意是 `exp`，**没有下划线**)。
- **GCC 14 / 15 的早期文档**里常写成 `-lstdc++_exp`(带下划线)。如果你的工具链还是老版本，照旧带下划线；新版改成不带。

本机实测：`-lstdc++_exp` 直接报 `cannot find -lstdc++_exp: No such file or directory`，改成 `-lstdc++exp` 才过。两条命令的区别只有一个字符，但能让你卡半天。

另外，这个库在你机器上**只有静态版 `libstdc++exp.a`，没有 `.so`**。所以 stacktrace 的实现是被**静态链进你的二进制**的，不会增加运行时动态依赖——这点对部署是好事，但代价是二进制会大几十 KB。
:::

一个完整的编译命令长这样：

```text
g++ -std=c++23 -O2 -g your_code.cpp -o your_app -lstdc++exp
```

如果你用 CMake，对应的是：

```cmake
target_link_libraries(your_app PRIVATE stdc++exp)
```

注意 `-lstdc++exp` 要放在源文件**后面**——GCC 的链接器是按顺序处理依赖的，库要出现在"需要它的目标"之后，否则符号照样解析不到。这也是一个经典的链接顺序坑。

## 第二个硬坑：调试符号决定你能拿到什么

链接过了，跑起来——但很快你会发现：为什么有时候 `source_file` 和 `source_line` 是空的？这一节回答这个问题。

关键在于：`<stacktrace>` 能给你的信息，分**两层数据源**，各自依赖不同的东西：

| 信息 | 数据源 | 依赖什么 |
|------|--------|----------|
| 函数名(`description`) | 运行时符号表(`.symtab` / `.dynsym`) | 符号没被 strip，或 `-rdynamic` 导出 |
| 源文件 + 行号(`source_file` / `source_line`) | DWARF 调试信息(`.debug_*` 段) | 编译时带 `-g` |

函数名来自符号表，源文件/行号来自调试信息——这是两套独立的东西。我们直接做对比实验，同一个程序分别用三种方式编译：

**带 `-g`(有调试信息)**：上面那段输出，`source_file` / `source_line` 全有。

**不带 `-g`(无调试信息，但符号表还在)**：

```text
--- entry 0 ---
  native_handle : 95551312581264
  description   : [inspect(int)]
  source_file   : []
  source_line   : 0
```

函数名照拿，但源文件和行号全空了——因为 `.debug_line` 段不存在，地址没法映射回源码位置。

**strip 掉符号表**(`g++ ... -g` 再 `strip`)：所有帧的 `description` 也全空了，只剩裸地址：

```text
--- entry 0 ---
  native_handle : 111239407198864
  description   : []
  source_file   : []
  source_line   : 0
```

`strip` 把 `.symtab` 删了，函数名也解析不出来。这时如果你在链接时加 `-rdynamic`(把符号导出到 `.dynsym` 动态符号表，strip 不会删 `.dynsym`)，函数名又能回来了：

```text
--- entry 0 ---
  description   : [inspect(int)]    # strip 后, 但链接时带了 -rdynamic
  source_file   : []                # 调试信息还是没了, 行号拿不到
```

这就是真实的工程取舍。给你的建议很直接：

::: warning 想拿到完整栈信息，编译期就要准备好数据源

- **想拿源文件 + 行号**：编译时必须带 `-g`(或 `-g3` 更详细)。发布版若想保留定位能力，可以 `objcopy --only-keep-debug` 把调试信息单独存成文件，运行时用 `addr2line -e app <addr>` 事后解析。
- **想拿函数名(在 strip 后)**：链接时加 `-rdynamic`，让符号进 `.dynsym`。代价是二进制变大、符号对外可见(有信息泄漏顾虑的话要权衡)。
- **生产环境最小栈信息**：至少带 `-rdynamic`，这样哪怕没调试信息、哪怕 strip 过，`description()` 至少还能给你函数名，不至于全是 `<unknown>`。
:::

### 原始 mangled 符号 vs description：为什么要 demangle

这里有个初学者容易混淆的点。C++ 因为有重载和命名空间，编译器会把函数名"捣碎"成一种内部表示(mangled name)。比如 `my_lib::compute_value(int, int)` 在符号表里实际存的是 `_ZN6my_lib13compute_valueEii`——这种东西人眼根本没法读。

`stacktrace_entry::description()` 已经替你**做了 demangle**，直接返回人话。我们拿 `dladdr`(libc 的地址→符号查询接口)对比一下，看看"原始"和"demangle 后"的差别：

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <dlfcn.h>      // dladdr
#include <cxxabi.h>     // abi::__cxa_demangle
#include <cstdlib>

namespace my_lib {
    int compute_value(int a, int b) {
        auto st = std::stacktrace::current();
        auto e = st[0];
        // stacktrace_entry 已经 demangle 过的描述
        std::cout << "description            : " << e.description() << '\n';

        // 用 dladdr 拿原始 mangled 符号做对比
        Dl_info info{};
        dladdr(reinterpret_cast<void*>(e.native_handle()), &info);
        std::cout << "dladdr dli_sname(原始) : " << (info.dli_sname ? info.dli_sname : "<null>") << '\n';

        // 手动 demangle 原始符号
        int status = 0;
        char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        std::cout << "手动 demangle          : " << (demangled ? demangled : "<null>") << '\n';
        std::free(demangled);
        return a + b;
    }
}

int main() {
    return my_lib::compute_value(1, 2) - 3;
}
```

`g++ -std=c++23 -O0 -g -rdynamic ... -lstdc++exp -ldl` 跑出来：

```text
description            : my_lib::compute_value(int, int)
dladdr dli_sname(原始) : _ZN6my_lib13compute_valueEii
手动 demangle          : my_lib::compute_value(int, int)
```

差别一目了然。`_ZN6my_lib13compute_valueEii` 是编译器内部的 mangled 名字(`_ZN` 开头是 g++ 的 C++ 名字标识，后面编码了命名空间、函数名、参数类型)，肉眼基本不可读。`stacktrace_entry::description()` 内部走的就是 `abi::__cxa_demangle` 这一套，直接给你 `my_lib::compute_value(int, int)`。所以日常用 `<stacktrace>` 你不需要自己 demangle——它已经帮你做了。只有在你要拿"原始符号串"做别的处理时(比如某些符号匹配工具)，才需要 `dladdr` 直接捞 mangled 名字。

## to_string 与 operator<<：两种打印方式

把一整张栈打印出来，有两种现成方式。

第一种是 `std::to_string(stacktrace)`——注意它是个**自由函数**，不是 `stacktrace` 的成员(写成 `st.to_string()` 会编译失败)。它返回一个 gdb 风格的多行字符串：

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
void level3() { auto st = std::stacktrace::current(); std::cout << std::to_string(st); }
void level2() { level3(); }
void level1() { level2(); }
int main() { level1(); }
```

输出长这样：

```text
   0#  level3() at /tmp/st_tostring.cpp:3 [0x57a7e83ed2cd]
   1#  level2() at /tmp/st_tostring.cpp:4 [0x57a7e83ed373]
   2#  level1() at /tmp/st_tostring.cpp:5 [0x57a7e83ed37f]
   3#  main at /tmp/st_tostring.cpp:6 [0x57a7e83ed38b]
   4#  <unknown> [0x7fe05d227740]
   5#  __libc_start_main [0x7fe05d227878]
   6#  _start [0x57a7e83ed1c4]
```

`序号#` + 函数 + `at 文件:行` + `[地址]`，读起来和 gdb 的 backtrace 输出几乎一样，这是标准库刻意对齐的格式。如果你要往日志里塞一整段栈，用它最省事。

第二种是 `operator<<`——它分两个重载：对单个 `stacktrace_entry` 输出单行，对整个 `basic_stacktrace` 等价于 `to_string`。单个 entry 的输出格式是「函数 at 文件:行 [地址]」(注意开头有个空格，这是标准规定的)：

```text
 foo(int) at /tmp/st_basic.cpp:6 [0x5b11f3c25e90]
```

`to_string` 适合"我要一整段、塞进日志"，`operator<<` 适合"我要流式输出、或者自己拼格式"。两者底层走的是同一套符号化逻辑，输出内容一致，只是封装粒度不同。

## 实战：在崩溃处理里打栈

最能体现 `<stacktrace>` 价值的就是崩溃诊断。程序收到 `SIGSEGV` 这类致命信号时，在 signal handler 里拍一张栈，比"程序闪退、什么都没留下"强太多。

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <csignal>
#include <cstdlib>

void log_stacktrace() {
    auto st = std::stacktrace::current();
    std::cerr << "=== stacktrace on crash ===\n";
    std::cerr << std::to_string(st);
}

void broken(int* p) {
    *p = 42;   // 故意空指针解引用, 触发 SIGSEGV
}

void outer(int n) {
    if (n == 0) broken(nullptr);
    outer(n - 1);
}

int main() {
    std::signal(SIGSEGV, [](int) {
        log_stacktrace();
        std::_Exit(1);   // 用 _Exit 避免析构链再出问题
    });
    outer(3);
    return 0;
}
```

`g++ -std=c++23 -O0 -g ... -lstdc++exp` 跑出来：

```text
=== stacktrace on crash ===
   0#  log_stacktrace() at /tmp/st_crash.cpp:7 [0x5a3f0683c2ce]
   1#  operator() at /tmp/st_crash.cpp:23 [0x5a3f0683c3d9]
   2#  _FUN at /tmp/st_crash.cpp:25 [0x5a3f0683c3fd]
   3#  <unknown> [0x7b645b63e8ef]
   4#  broken(int*) at /tmp/st_crash.cpp:13 [0x5a3f0683c391]
   5#  outer(int) at /tmp/st_crash.cpp:17 [0x5a3f0683c3b4]
   6#  outer(int) at /tmp/st_crash.cpp:18 [0x5a3f0683c3c1]
   7#  outer(int) at /tmp/st_crash.cpp:18 [0x5a3f0683c3c1]
   8#  outer(int) at /tmp/st_crash.cpp:18 [0x5a3f0683c3c1]
   9#  main at /tmp/st_crash.cpp:26 [0x5a3f0683c44a]
  10#  <unknown> [0x7b645b627740]
  11#  __libc_start_main [0x7b645b627878]
  12#  _start [0x5a3f0683c1c4]
```

这条栈直接告诉你崩溃发生在 `broken`，是从 `main` 一路递归调 `outer` 进去的——排查时一眼定位。这里有几个真实工程要注意的点：

- 栈顶几帧是**signal handler 自己**(`log_stacktrace`、lambda 的 `operator()`、`_FUN`、内核的 `sigreturn` 跳板 `<unknown>`)。真正的崩溃点在它们**下面**的 `broken` 那一帧。读崩溃栈时记得先把 handler 自己的几帧跳过。
- signal handler 是**异步信号上下文**，不是普通函数调用。`std::to_string` 内部会分配内存(`new` / `malloc`)，严格说在信号处理里调非 async-signal-safe 的函数是有风险的。本例用 `std::_Exit`(async-signal-safe)退出，降低风险；对绝对严谨的场景，更稳的做法是在 handler 里只设个 flag、在主循环里再采集，或用 `sigaltstack` 配合专门的处理栈。但作为"崩溃留证据"的轻量方案，上面这段在工程里广泛够用。
- 想让 handler 里的栈也带行号，崩溃的这个二进制同样要带 `-g`，否则 `broken` 那帧也只剩函数名。

## 性能：采集便宜，符号化贵

前面埋了个伏笔：`current()` 只采集地址，符号化是访问 entry 时才发生。这意味着开销分两段，数量级差很多。我们实测一把——递归 5 层后采集(深度 6)，分别测"只采集"和"采集 + `to_string` 全符号化"，各跑 10 万次：

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <chrono>
#include <string>

void deep(int n) {
    if (n == 0) {
        auto st = std::stacktrace::current();
        volatile auto sz = st.size();   // 防止被优化掉, 但不触发符号化
        (void)sz;
        return;
    }
    deep(n - 1);
}

int main() {
    constexpr int kIters = 100000;
    for (int i = 0; i < 1000; ++i) deep(5);   // 预热

    // 只采集
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) deep(5);
    auto t2 = std::chrono::high_resolution_clock::now();
    double ns_capture =
        std::chrono::duration<double, std::nano>(t2 - t1).count() / kIters;

    // 采集 + 全符号化
    int sink = 0;
    t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto st = std::stacktrace::current();
        std::string s = std::to_string(st);
        sink += static_cast<int>(s.size());
    }
    t2 = std::chrono::high_resolution_clock::now();
    double ns_full =
        std::chrono::duration<double, std::nano>(t2 - t1).count() / kIters;

    std::cout << "depth=6, iters=" << kIters << '\n';
    std::cout << "capture-only       : " << ns_capture << " ns/call\n";
    std::cout << "capture + to_string: " << ns_full << " ns/call\n";
    std::cout << "sink=" << sink << '\n';
    return 0;
}
```

`g++ -std=c++23 -O2 -g ... -lstdc++exp`，连跑两次看稳定性：

```text
depth=6, iters=100000
capture-only       : 841.43 ns/call
capture + to_string: 2145.12 ns/call
sink=15900000

depth=6, iters=100000
capture-only       : 852.22 ns/call
capture + to_string: 1954.18 ns/call
sink=15900000
```

数字很说明问题。本机(x86-64, GCC 16.1.1, `-O2`)数量级：

- **只采集**：约 **0.8 µs / 次**。深度只有 6，主要开销在遍历栈帧、读返回地址。热路径上偶尔拍一张完全能接受。
- **采集 + 全符号化**：约 **2 µs / 次**，是只采集的 2~3 倍。多出来的开销是 demangle、字符串拼接、内存分配(`std::string`)。深度越大、符号越长，这部分涨得越多。

::: warning 绝对值因机器而异, 数量级才稳
上面是本机空闲时的测量, 绝对值会随 CPU 负载、栈深度、符号长度波动(两次跑的符号化耗时差了快 10%)。但**数量级关系是稳的**: 符号化比纯采集贵数倍, 且都远比一次普通函数调用贵(纳秒级)。结论是——**热循环里别随手 `to_string`**, 只在错误路径、诊断路径上才符号化。
:::

这个"采集便宜、符号化贵"的拆分，正是标准库把两者解耦的设计动机。你可以先 `current()` 拿到轻量的栈快照存起来(几乎零成本)，真要诊断时再 `to_string`——比如把采集来的 `stacktrace` 对象塞进日志队列，由后台线程慢慢符号化，不阻塞业务。如果一开始就把符号化和采集绑死，这种延迟符号化就不可能了。

## 和 source_location：什么时候用哪个

`<stacktrace>` 有个气质很像的兄弟——C++20 的 `std::source_location`(本卷 68 篇)。两者都能告诉你"代码在哪里"，但定位完全不同，**别用混**：

| 维度 | `std::stacktrace`(C++23) | `std::source_location`(C++20) |
|------|--------------------------|-------------------------------|
| 给什么 | 运行时**整条调用链**(多帧) | 编译期**单个点**(当前函数/文件/行) |
| 何时确定 | 运行时采集 | 编译期固定 |
| 开销 | 微秒级(采集 + 符号化) | **零开销**(编译期常量) |
| 典型场景 | 崩溃诊断、错误日志、调试追踪 | 日志打点、断言、默认参数里"我在哪" |

最直观的差别是开销和粒度。`source_location` 是编译期常量，编译器直接填好 `__FILE__` / `__LINE__` / 函数名，运行时拿就是读几个常量，零成本，所以它可以放心地用在日志的每一行、每个断言里。`stacktrace` 是运行时实采，有微秒级开销，只该用在"出了事、值得花这个代价搞清楚来龙去脉"的地方。

一个常见的工程搭配：**平时打日志用 `source_location`** 带上当前函数和行号(零开销、够定位单点)；**到了错误/崩溃路径，再用 `stacktrace`** 把整条调用链拍下来(贵但信息全，值得)。下一篇我们会专门讲 `source_location`，这里先建立这个分工直觉就够了。

## 小结

`std::stacktrace` 的核心就这么几条，收一下：

- **两层结构**：`basic_stacktrace`(帧序列，可 `size()` / 下标 / 遍历) + `stacktrace_entry`(单帧，按需查 `description()` / `source_file()` / `source_line()`)。采集和符号化解耦——`current()` 只拿地址，符号化在访问 entry 时才发生。
- **链接库是第一个坑**：libstdc++ 的 `<stacktrace>` 实现在实验库里，默认不链。GCC 16 用 `-lstdc++exp`(无下划线)，GCC 14/15 旧文档是 `-lstdc++_exp`(带下划线)。不链就 `undefined reference`，链错名就 `cannot find`。该库只有静态版 `libstdc++exp.a`，会被静态链进二进制。
- **调试符号是第二个坑**：函数名靠符号表(需未 strip 或 `-rdynamic`)，源文件/行号靠 DWARF 调试信息(需 `-g`)。strip 掉符号表后只剩裸地址。生产环境至少带 `-rdynamic` 保住函数名。
- **description 已 demangle**：`_ZN6my_lib13compute_valueEii` 这种 mangled 名字，`description()` 会自动还原成 `my_lib::compute_value(int, int)`，日常不用自己接 `abi::__cxa_demangle`。
- **两种打印**：`std::to_string(st)` 自由函数给 gdb 风格多行串；`operator<<` 分单 entry(单行)和整栈。
- **性能**：采集约 0.8 µs、符号化后约 2 µs(本机、深度 6、`-O2`)。数量级上符号化贵数倍——热路径只采集，错误路径再符号化。
- **和 source_location 分工**：`stacktrace` 是运行时整栈、有开销，用于崩溃/诊断；`source_location` 是编译期单点、零开销，用于日志打点/断言。两者搭配用，不是替代关系。

到这里，C++23 终于把"运行时调用栈采集"这件各平台各显神通的事标准化了。下一篇我们转向它的零开销兄弟 `source_location`——看看编译期那一套是怎么零成本拿到代码位置的。

## 参考资源

- [cppreference: std::basic_stacktrace (C++23)](https://en.cppreference.com/w/cpp/utility/basic_stacktrace) —— `current()` / `size()` / 遍历接口与 `std::stacktrace` 别名
- [cppreference: std::stacktrace_entry (C++23)](https://en.cppreference.com/w/cpp/utility/stacktrace_entry) —— `description` / `source_file` / `source_line` / `native_handle` 的语义(标准里没有 `symbol()` 成员)
- [cppreference: std::to_string (stacktrace)](https://en.cppreference.com/w/cpp/utility/basic_stacktrace/to_string) —— 自由函数 `to_string` 的 gdb 风格输出格式
- [cppreference: `__cpp_lib_stacktrace`](https://en.cppreference.com/w/cpp/feature_test) —— 特性测试宏，本机 GCC 16.1.1 实测值为 `202011`
- [GCC libstdc++ C++23 status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html#iso.2023) —— `<stacktrace>` 实现状态与实验库链接约定
