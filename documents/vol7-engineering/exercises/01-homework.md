---
title: "卷 7 课后练习（Homework）"
description: "软件工程实践卷的课后练习：8 章每章 2 题（基础+进阶），另加 2 道跨章综合与 1 道 L5 挑战（编译依赖 DAG 关键路径，受 UVa 452 启发改编）。难度覆盖 L1~L5，题目都做变式处理，参考答案独立成文件、逐步解答附知识点链接。所有 CMake 构建一律在 /tmp 独立目录做。"
chapter: 7
order: 1
tags: [host, intermediate, cpp-modern, CMake, 工程实践]
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 15
prerequisites:
  - "卷 7 全部章节（第 7.1~7.8 章）"
related:
  - "卷 7 Lab：发布流水线"
  - "卷 7 Project：fcopy++ 工程化文件拷贝器"
---

# 卷 7 课后练习（Homework）

## 引言

这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](./index.md)）和涉及章节；题目都是「变式」——换场景、换数据、换推理方向，照抄教材例题抄不出答案。每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](./02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一章做完再看答案。所有代码用 `g++ -std=c++17 -Wall -Wextra` 起步（个别题目要求 CMake、模块或 sanitizer，题面会写明）。一条本卷铁律：**CMake 构建一律在 /tmp 下的独立目录做**，别把 build 产物留在任何源码树里。

## 7.1 交叉编译与 CMake

### 7.1-A {#hw-7-1-a}

难度 **L1** · 涉及[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

在 `/tmp` 下建一个 `hello_v7/` 工程：一个 `CMakeLists.txt` + 一个打印 `Hello, vol7!` 的 `main.cpp`，用 `cmake -S . -B build` 配置、`cmake --build build` 构建、运行。贴出配置和构建的真实输出。然后回答三问：①`-S`/`-B` 这两个参数分别指定什么？为什么它天然是 out-of-source 构建？②CMake 的「生成器」指的是什么？③教材里说交叉编译必须通过 `-DCMAKE_TOOLCHAIN_FILE` 在**第一次**配置时传入——结合你的 `build/CMakeCache.txt` 说说「缓存」在扮演什么角色。

[参考答案 →](./02-homework-solutions.md#hw-7-1-a)

### 7.1-B {#hw-7-1-b}

难度 **L2** · 涉及[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

写一个 `greet/` 工程：静态库 `greeter_static`（`greeter.hpp` + `greeter.cpp`，导出 `greet::make_greeting`）+ 可执行 `app`。**先写一个坏版本**：用全局 `include_directories(include)`、并且 `target_link_libraries(app PRIVATE greeter_typo)`（名字打错）。预期哪里会炸？配置能过吗、构建能过吗？贴出真实报错，解释「裸名字被打成 `-lgreeter_typo`」意味着什么。然后修成现代写法：`target_include_directories(... PUBLIC ...)` + 正确的 target 链接，跑通并贴出输出。最后加一组实验钉死 `PUBLIC`/`PRIVATE` 的语义：给库挂 `PUBLIC` 和 `PRIVATE` 各一个 `target_compile_definitions`，在 `app` 里用 `#ifdef` 检测两个宏——哪个可见、哪个不可见？贴出结果。

[参考答案 →](./02-homework-solutions.md#hw-7-1-b)

## 7.2 编译器选项

### 7.2-A {#hw-7-2-a}

难度 **L1** · 涉及[编译器选项](../02-compiler-options.md)

写一个「带病」的程序：`if (x = 5)`（赋值写成比较）、一个定义了从不使用的变量、还有一个 `-D` 控制的分支（`#ifdef DEBUG_LEVEL` 打印级别）。三步实验都要真跑：①`-Wall -Wextra` 编译，数一数有几条警告、各属于哪个 `-W` 家族；②加上 `-Werror` 再编译，贴出报错，说明 CI 里为什么推荐这么干；③`-DDEBUG_LEVEL=2` 编译运行，再不带 `-D` 编译运行，对比两次输出。最后把程序修到 `-Wall -Wextra -Werror` 零警告，用 `-DDEBUG_LEVEL=3` 跑一遍收尾。

[参考答案 →](./02-homework-solutions.md#hw-7-2-a)

### 7.2-B {#hw-7-2-b}

难度 **L3** · 涉及[编译器选项](../02-compiler-options.md)、[链接器与链接脚本](../03-linker-and-linker-scripts.md)

三组量化实验。①**优化等级实测**：写一个累加 `0..500M` 的循环（结果存入 `volatile` 变量防止被整体删除），分别在 `-O0`/`-O2`/`-Os` 下编译，用 `size` 对比体积、各跑 3 次记录自测耗时——先预测 `-O2` 和 `-O0` 会差多少倍，再真跑，如果你的预测差了 100 倍以上，用编译原理解释发生了什么。②**`-Wdouble-promotion` 抓包**：写 `float half(float x) { return x * 0.5; }`，贴出这条警告，解释 $0.5$ 是什么类型、这条警告在无硬件双精度浮点的单片机上为什么值钱。③**gc-sections 量化**：把两个函数分放两个 `.cpp`，`main` 只调用其中一个；分别用「普通编译链接」和「`-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`」构建，对比 `size` 输出，并用 `nm` 证明那个没人调用的函数在第二种构建里消失了。

[参考答案 →](./02-homework-solutions.md#hw-7-2-b)

## 7.3 链接器与链接脚本

### 7.3-A {#hw-7-3-a}

难度 **L2** · 涉及[链接器与链接脚本](../03-linker-and-linker-scripts.md)

写两个翻译单元：`libpart.cpp` 定义 `int g_counter = 10;`、`const char* g_version = "v1.0";`、`int bump_counter()` 和一个 `static int local_helper()`；`usepart.cpp` 声明 `extern int g_counter;` 并调用 `bump_counter()`。分别编译成 `.o`，用 `nm` 观察两份符号表并回答：①`T`/`t`/`D`/`U` 各代表什么？`local_helper` 的小写 `t` 和 `bump_counter` 的大写 `T` 差在哪？②链接后 `nm` 可执行文件，`local_helper` 还在不在符号表里（注意它是 `static`）？③再做一个「符号解析失败」实验：声明 `void launch_missiles();` 并调用但任何地方都不定义，贴出链接器的真实报错——它报的是什么阶段、什么符号？

[参考答案 →](./02-homework-solutions.md#hw-7-3-a)

### 7.3-B {#hw-7-3-b}

难度 **L3** · 涉及[链接器与链接脚本](../03-linker-and-linker-scripts.md)

两个实验。①**静态初始化顺序 fiasco 实测**：`a.cpp` 和 `b.cpp` 各定义一个带构造函数的全局对象，构造函数里各打印一行；先预测「两个构造函数谁先跑」，再用两种链接顺序（`g++ a.cpp b.cpp main.cpp` vs `g++ b.cpp a.cpp main.cpp`）真跑，贴出两份输出——顺序变了吗？然后写一个 Meyers 单例版（函数局部 `static`），证明它「首次调用才构造、只构造一次」。②**`.bss` 与 `.data` 的体积真相**：写一个程序同时含 `static char big_zero[8*1024*1024];` 和 `static char big_data[8*1024*1024] = {1};`，编译后 `ls -l` 看文件大小、`size` 看段布局——为什么一个 8MB 的数组几乎不占文件空间、另一个占满 8MB？结合教材「`.bss` 不占用 FLASH」的说法讲清楚。

[参考答案 →](./02-homework-solutions.md#hw-7-3-b)

## 7.4 文件拷贝器（上）：需求分析与基础框架

### 7.4-A {#hw-7-4-a}

难度 **L1** · 涉及[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)

把教材的 `FileCopier` 骨架复现一遍：`fcopy.h`（类声明：`explicit` 构造函数带默认 `chunk_size = 8 * 1024`、`copy` 方法、`set_chunk_size`）+ `fcopy.cpp`（构造函数用成员初始化列表、`copy` 先返回 `false` 占位）。要求 `g++ -std=c++17 -Wall -Wextra -c` 编译**零警告**。然后回答三问：①`explicit` 在这里防的是什么？写出「不加 explicit 会悄悄编译通过」的那行隐式转换代码。②为什么缓冲区选 `std::vector<char>` 而不是 `new char[]`？③`copy` 的参数为什么是 `const std::string&` 而不是按值传？

[参考答案 →](./02-homework-solutions.md#hw-7-4-a)

### 7.4-B {#hw-7-4-b}

难度 **L2** · 涉及[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)

实现拷贝前的「前置检查 + 文件打开」环节：`fs::exists` 检查、`fs::file_size` 取大小、二进制模式打开两个流、分配 `vector<char>` 缓冲，全通过则打印一行 `prepared: src=... size=... chunk=...`。整个函数包在 `try-catch` 里（先捕 `filesystem_error` 再捕 `std::exception`）。用四个用例真跑并贴出**全部**输出：①正常小文件；②不存在的源文件；③目标路径在一个不存在的目录里；④**源是一个目录**——这个用例最阴险：`exists` 能过、`file_size` 会怎样？贴出它走的是哪条错误路径。

[参考答案 →](./02-homework-solutions.md#hw-7-4-b)

## 7.5 文件拷贝器（下）：核心实现与实战测试

### 7.5-A {#hw-7-5-a}

难度 **L2** · 涉及[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)

实现核心读写循环并验证「最后一块」：`while (in)` 循环里 `read` + `gcount` + `write`，每块写完立刻检查流状态，循环后 `flush`/`close`、再比较 `copied` 与 `total_size`。用两个用例真跑：①`dd` 造一个 **100003 字节**的文件（不是块大小的整数倍），`chunk=4096` 拷贝——打印共几块？最后一块多大？`md5sum` 前后对照贴出来；②空文件拷贝——几块？目标文件大小多少？为什么循环体一次都没进？顺带回答：`while (in)` 和 `while (!in.eof())` 差在哪，为什么教材用前者？

[参考答案 →](./02-homework-solutions.md#hw-7-5-a)

### 7.5-B {#hw-7-5-b}

难度 **L3** · 涉及[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)、[编译器选项](../02-compiler-options.md)

块大小到底影不影响速度？做一个基准：`dd` 造 64MB 文件，用上一题的拷贝核心（`-O2`）分别以 $4096$/$65536$/$1048576$ 三档块大小各拷 3 次，用 `std::chrono::steady_clock` 自测耗时并换算 MB/s，打印一张对比表。贴出你的真实数据（数值每台机器不同，趋势应当一致）。然后解释：①为什么块越小越慢（结合系统调用次数）；②为什么块再大（比如 16MB）收益就没了；③教材默认 8KB 在「内存压力」和「系统调用开销」之间做了什么样的折中。

[参考答案 →](./02-homework-solutions.md#hw-7-5-b)

## 7.6 WSL 开发 C++

### 7.6-A {#hw-7-6-a}

难度 **L2** · 涉及[WSL 开发 C++](../cpp-development-on-wsl.md)、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

在 WSL 里写一个「环境自报」程序：打印 `__cplusplus` 的值、编译器身份（`__GNUC__` 系列或 `__clang__` 系列）、标准库身份（`__GLIBCXX__` 或 `_LIBCPP_VERSION`）。把它放进一个最小 CMake 工程，分别用 `-G "Unix Makefiles"` 和 `-G Ninja` 两个生成器各配置构建一次，贴出两次的输出——程序输出应当一样吗？构建日志的差异在哪？再回答：教材里 `cmake .. -G "Ninja"` 这一句，`-G` 参数在 CMake 的「配置/生成/构建」三阶段里影响的是哪一阶段？

[参考答案 →](./02-homework-solutions.md#hw-7-6-a)

### 7.6-B {#hw-7-6-b}

难度 **L2** · 涉及[WSL 开发 C++](../cpp-development-on-wsl.md)

「换 kit」实验：同一个 7.6-A 的工程，用 `cmake -S . -B build-clang -G Ninja -DCMAKE_CXX_COMPILER=clang++` 换 clang 构建，运行并贴出输出——特别注意 `__GNUC__` 那行，它打印的是什么版本？clang 为什么会「冒充」GCC？这告诉我们判断编译器该先看哪个宏？然后**在同一个构建目录里**再传一次 `-DCMAKE_CXX_COMPILER=g++`，观察 CMake 的反应（贴出它的提示），说说工程上为什么换工具链最好直接删掉构建目录重来。

[参考答案 →](./02-homework-solutions.md#hw-7-6-b)

## 7.7 VS2026 使用 C++ 模块

### 7.7-A {#hw-7-7-a}

难度 **L1** · 涉及[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)

三问 + 一跑。问答：①教材说模块把「增量编译分析到了二进制 ABI 层次」，这里的 BMI 是什么东西、VS2026 用它做什么？②`export module math;`、`export int add(...)`、`import math;` 三个关键字分别在做什么？③`.ixx` 和 `.cppm` 有什么区别、MSVC 和 GCC/Clang 各认哪个？动手：在 WSL 上把教材的 `math` 模块最小示例跑通（题面提示：本机 g++ 16 要用 `-std=c++20 -fmodules`，`g++ -c math.cppm -o math.o` 先编译模块再链接；Windows 上 VS2026 的对应流程就是教材那套 IDE 操作），贴出真实输出。

[参考答案 →](./02-homework-solutions.md#hw-7-7-a)

### 7.7-B {#hw-7-7-b}

难度 **L3** · 涉及[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

搭一条三环模块链：`core`（导出 `scale`）→ `report`（`import core;` 后导出 `as_percent`）→ `main`（`import report;`）。用 CMake 4 的 `FILE_SET CXX_MODULES` + `target_compile_options(... -fmodules)` 构建（教材的 MSVC 侧由 IDE 自动扫描，GCC 侧就这么配），跑通后做三个「触碰实验」，每次用 `ninja -C build -v` 贴出**实际被重新编译的文件清单**：①`touch main.cpp`——谁被重编？②`touch core.cppm`（接口模块！）——谁被重编？③`touch report.cppm`——谁被重编、`core.cppm.o` 为什么没动？再跑一次零改动构建，看 Ninja 说什么。三个实验合起来就是「BMI 缓存 + 依赖扫描」的完整证据链。

[参考答案 →](./02-homework-solutions.md#hw-7-7-b)

## 7.8 MSVC 调试原理

### 7.8-A {#hw-7-8-a}

难度 **L2** · 涉及[MSVC 调试原理](../msvc-debugging-internals.md)

三问 + 一跑。问答：①教材的「调试三能力」观测/控制/映射各对应调试器的哪个动作（举 GDB 或 VS 的具体操作）？②调试链路上 IDE、调试引擎、msvsmon、内核各管什么——为什么要有 msvsmon 这个中间层？③PDB 里存了什么？「Release 模式空心断点」通常是什么原因？动手：同一程序分别用「`-g`」和「不带 `-g`」编译，`ls -l` 对比大小、`readelf -S` 数一下 debug 段的数量，贴出结果——对照教材「`-g` 不会增加烧进单片机的体积」的说法，说明调试信息到底住在哪。

[参考答案 →](./02-homework-solutions.md#hw-7-8-a)

### 7.8-B {#hw-7-8-b}

难度 **L3** · 涉及[MSVC 调试原理](../msvc-debugging-internals.md)、[编译器选项](../02-compiler-options.md)

三个真刀真枪的调试实验（gdb 批处理模式即可，不需要图形界面）。①**断点漂移**：写一个带循环的 `compute(int)`（标 `__attribute__((noinline))`），分别在 `-O0 -g` 和 `-O2 -g` 下 `break compute`，对比断点落到的行号——为什么 `-O2` 下断点不在函数第一行？gdb 显示的 `n=n@entry=10` 这个注解是什么意思？②**栈损坏现场**：写一个 `strcpy(buf, input)` 且 `buf[8]` 的 `victim`，用 `-fno-stack-protector -g` 编译，输入 32 个 `A`——先短输入跑通，再长输入看崩溃；在 gdb 里断在 `victim` 入口打一次 `bt`，`continue` 到崩溃后再打一次 `bt`，贴出那份「地址变成 `0x4141414141414141`」的栈回溯，说说 `0x41` 是什么。③**让 ASan 抓现行**：同一份代码用 `-fsanitize=address` 构建，贴出 ASan 报告（它点名了哪个变量、溢出发生在哪一行、退出码是多少）。

[参考答案 →](./02-homework-solutions.md#hw-7-8-b)

## 7.C 跨章综合与挑战

### 7.C-1 {#hw-7-c-1}

难度 **L3** · 涉及[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)、[编译器选项](../02-compiler-options.md)、[文件拷贝器（下）](../02-file-copier-core-implementation.md)

综合题：把拷贝器工程化。建一个 `fcopy_proj/` 工程：`include/fcopy.h` + `src/fcopy.cpp`（完整拷贝核心）+ `src/main.cpp` + `tests/run_tests.sh` + 顶层 `CMakeLists.txt`。要求：①警告旗标（`-Wall -Wextra -Wpedantic -Werror`）做成 `INTERFACE` 库 `project_warnings`，所有 target 链接继承；②拷贝核心建成静态库 `fcopy_core`，`target_include_directories` 用 `PUBLIC`；③`option(FCOPY_BUILD_TESTS ...)` 控制测试开关，`enable_testing()` + `add_test` 用 `$<TARGET_FILE:fcopy>` 把二进制路径传给测试脚本；④测试脚本至少覆盖：非整块大小文件逐字节一致（`cmp -s`）、空文件、源不存在必须非零退出。贴出 configure 与 build 的完整输出、`ctest --test-dir build --output-on-failure` 的真实结果。

[参考答案 →](./02-homework-solutions.md#hw-7-c-1)

### 7.C-2 {#hw-7-c-2}

难度 **L4** · 涉及[链接器与链接脚本](../03-linker-and-linker-scripts.md)、[编译器选项](../02-compiler-options.md)

ELF 考古。写一个两文件程序（一个含 `global_data`（非零初始化）、`zero_data`（零初始化）、`static helper()`；另一个含没人调用的 `never_called()`），用 `-Wl,-Map=plain.map` 链接生成 map 文件。回答并贴证据：①在 map 里找到 `main` 的地址、`.data` 段的起始地址，以及 `global_data` 和 `zero_data` 各自的地址——两个变量落在同一段吗？哪个在 `.bss`？②对照教材链接脚本里的 `MEMORY`/`SECTIONS`/`> FLASH`/`> RAM` 这些概念，说明宿主机的默认链接脚本是「谁提供的」、为什么嵌入式必须自己写而宿主机不用。③再做「垃圾回收考古」：用 `-ffunction-sections -fdata-sections -Wl,--gc-sections` 重新链接，对比两次的 `objdump -h` 段数量、`size` 的 text 大小，并在两次 map/`nm` 里找 `never_called`——它是怎么消失的？教材里对应哪对编译/链接选项？

[参考答案 →](./02-homework-solutions.md#hw-7-c-2)

### 7.C-3 {#hw-7-c-3}

难度 **L5** · 涉及[编译器选项](../02-compiler-options.md)、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

挑战题（**受 UVa 452「Project Scheduling」启发改编**：任务换成编译单元、依赖换成头文件依赖；本卷 L5 口径＝「复杂构建场景攻坚」类，见[练习总览](./index.md)）。写一个「编译计划器」：输入 N 个编译任务，每个任务有名字、编译耗时（秒）和依赖列表（`#include` 依赖的头文件/目标文件，依赖必须先完成），任务输入顺序任意。计算：①每个任务的最早完成时间；②**并行编译下的最短总时间**（即 DAG 关键路径长度）；③输出一条关键路径。要求：用 Kahn 拓扑排序 + 沿拓扑序的动态规划，处理两类坏输入——依赖了不存在任务、存在循环依赖（就像两个头文件互相 `#include`）都要报错退出。用题面给的三组数据验证：第一组答案应是 **45 s**，第二组应是 **67 s**，第三组应报循环依赖。全部用 `std::map` + `std::queue`/`std::vector` 完成，`-Wall -Wextra` 零警告。数据格式（每组先一行 N，接着 N 行「名字 耗时 依赖数 [依赖名…]」）：

```text
5
core.hpp 5 0
core.o 30 1 core.hpp
util.o 15 1 core.hpp
app.o 20 1 core.hpp
main.o 10 2 app.o core.o
```

第二组、第三组数据见答案文件（先自己做，别看答案）。

[参考答案 →](./02-homework-solutions.md#hw-7-c-3)
