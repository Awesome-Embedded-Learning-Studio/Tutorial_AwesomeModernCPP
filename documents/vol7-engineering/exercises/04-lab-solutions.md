---
title: "卷 7 Lab 实验参考"
description: "发布流水线 Lab（analyzer 工程化六步 + 矩阵攻坚）的实验参考：逐步解答、每步标注知识点链接，含警告门、target 语义、段与符号、gdb/ASan 调试、C++20 模块化与八组合矩阵构建的真实输出，全部在 WSL Arch（g++ 16 / clang++ 22 / CMake 4.4.2）的 /tmp 目录真实运行得到。"
chapter: 7
order: 4
tags: [host, intermediate, cpp-modern, CMake, 调试]
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 16
prerequisites:
  - "卷 7 Lab 题面"
related:
  - "卷 7 全部章节（第 7.1~7.8 章）"
---

# 卷 7 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1、clang++ 22.1.8、CMake 4.4.2、Ninja 1.13.2、gdb 17.2）下真实运行得到；全部构建在 `/tmp` 独立目录完成。建议卡住时先看「思路」逐步对照。

## 步骤 1：最小 CMake 工程 {#lab-1}

**思路**：三文件里 `analyzer_core` 是纯函数（吃字符串、吐计数），`main` 管 I/O——这个切法让后面拆库、模块化都顺理成章。手算样本：12 + 16 + 4 = 32 字节、3 行、6 词。

1. `count_text`：`text.size()` 记字节，`'\n'` 记行，`isspace` 切词（`in_word` 状态位避免连续空白重复计数）。→ 知识点：[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)（流与循环的工程写法同源）
2. `main` 用 `istreambuf_iterator` 一把读全文件（小文本够用；大文件要分块，那是 Homework 7.5 的活）。→ 知识点：[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)「文件流」一节
3. 配置：`-S` 指源码树、`-B` 指构建树、`-G Ninja` 选生成器；`CMAKE_BUILD_TYPE=Debug` 让后续调试旗标生效。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「CMake 基本概念」一节
4. 真跑输出 `bytes=32 lines=3 words=6`，与手算一致。→ 知识点：[WSL 开发 C++](../cpp-development-on-wsl.md)「创建一个最小 CMake + C++ 项目」一节

**验证输出**：

```text
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
-- Configuring done (2.7s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cpp-v7-lab15/lab1/build
$ cmake --build build
[1/3] Building CXX object CMakeFiles/analyzer.dir/analyzer_core.cpp.o
[2/3] Building CXX object CMakeFiles/analyzer.dir/main.cpp.o
[3/3] Linking CXX executable analyzer
$ ./build/analyzer sample.txt
bytes=32 lines=3 words=6
```

## 步骤 2：警告全歼 {#lab-2}

**思路**：三颗雷分别触发 `-Wshadow`、`-Wsign-compare`、`-Wunused-variable`；`-Werror` 把它们全部升级成 error，构建退出码 1——这就是 CI 门禁的机制。

1. `main_bad.cpp` 的循环里 `int threshold = i * 2;` 遮蔽外层 → `-Wshadow`；`count < i` 有符号混比 → `-Wsign-compare`；`never_used` → `-Wunused-variable`。→ 知识点：[编译器选项](../02-compiler-options.md)「警告治理：`-W` 系列」一节
2. `-Werror` 后同样的三处变 error：`cc1plus: all warnings being treated as errors`。→ 知识点：同上（CI 推荐实践）
3. 修复：内层改名 `doubled`、比较显式 `static_cast<unsigned int>(i)`、删死变量——全套旗标零警告。→ 知识点：同上

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow main_bad.cpp -o main_bad
main_bad.cpp:7:13: warning: declaration of 'threshold' shadows a previous local [-Wshadow]
main_bad.cpp:14:19: warning: comparison of integer expressions of different signedness: 'unsigned int' and 'int' [-Wsign-compare]
main_bad.cpp:20:9: warning: unused variable 'never_used' [-Wunused-variable]
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Werror main_bad.cpp -o main_bad2; echo "exit=$?"
main_bad.cpp:7:13: error: declaration of 'threshold' shadows a previous local [-Werror=shadow]
main_bad.cpp:14:19: error: comparison of integer expressions of different signedness: 'unsigned int' and 'int' [-Werror=sign-compare]
main_bad.cpp:20:9: error: unused variable 'never_used' [-Werror=unused-variable]
cc1plus: all warnings being treated as errors
exit=1
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -Wshadow -Werror main_fixed.cpp -o main_fixed && ./main_fixed
0 2 4 6 8 10 12 14 16 18
count=3
```

## 步骤 3：target 语义 {#lab-3}

**思路**：`PUBLIC` 的 include 目录和编译定义会「传染」给链接者，`PRIVATE` 只在库自己编译时生效——app 的两行 `#ifdef` 就是现场证词。

1. `target_include_directories(... PUBLIC ...)`：app 链接库即可 include 头文件，不需要任何手动 `-I`。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「平台抽象层（HAL）设计」一节
2. `PUBLIC CORE_PUBLIC_FLAG` 可见、`PRIVATE CORE_PRIVATE_FLAG` 不可见——运行输出两行实锤。→ 知识点：同上（PUBLIC/PRIVATE 传播语义）

**验证输出**：

```text
$ cmake -S . -B build -G Ninja && cmake --build build
[1/4] Building CXX object CMakeFiles/analyzer_core.dir/src/analyzer_core.cpp.o
[2/4] Linking CXX static library libanalyzer_core.a
[3/4] Building CXX object CMakeFiles/analyzer.dir/src/main.cpp.o
[4/4] Linking CXX executable analyzer
$ ./build/analyzer
PUBLIC flag visible to app
PRIVATE flag not visible to app: OK
bytes=14 lines=2 words=3
```

本步的 `main` 在打完两行宏检测后，对内嵌样本串 `"foo bar\nbazzz\n"` 计数——两行 `foo bar`、`bazzz` 都以 `\n` 结尾，共 14 字节（含 2 个换行字节）、2 行、3 词。行计数口径与步骤 1 相同：按 `'\n'` 的个数记行，所以「两行」必须带结尾换行，缺尾换行的样本（如 `foo bar\nbazzz`）只会数出 1 行；步骤 1 的 `sample.txt` 三行各带 `\n`，因此 `lines=3`。

## 步骤 4：段与符号侦探 {#lab-4}

**思路**：`nm` 看符号、`size`/`objdump -h` 看段；两个体积实验一个来自「零值不必存」、一个来自「死代码可以收」。

1. `nm -C` 静态库里 `count_text` 是 `T`（全局函数定义）；可执行文件 `.text=0xcb7`、`.data=0x18`、`.bss=0x118`。→ 知识点：[链接器与链接脚本](../03-linker-and-linker-scripts.md)「链接器的四大核心任务」与「不同段的作用」两节
2. 大数组：文件 4210392 字节 ≈ 4MB，`size` 显示 data=4194952——`big_data` 的非零初值全进了镜像；`big_zero` 在 `.bss`（4194336 字节）却不占文件空间，启动代码运行时清零。→ 知识点：同上（`.bss` 不占用 FLASH 空间）
3. gc：text 1301→1267，`never_called` 的 `T` 符号消失——`-ffunction-sections` 把每个函数装进独立段，`--gc-sections` 按引用图回收。→ 知识点：[编译器选项](../02-compiler-options.md)「垃圾回收不用的代码」一节

**验证输出**：

```text
$ nm -C ../lab3/build/libanalyzer_core.a | grep count_text
0000000000000000 T an::count_text(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)
$ size ../lab3/build/analyzer
   text    data     bss     dec     hex filename
  10033     728     280   11041    2b21 ../lab3/build/analyzer
$ objdump -h ../lab3/build/analyzer | grep -E "Idx|\.text|\.data|\.bss|\.rodata"
 11 .text         00000cb7  00000000000020f0  00000000000020f0  000020f0  2**4
 13 .rodata       000000c2  0000000000003000  0000000000003000  00003000  2**3
 25 .data         00000018  0000000000005060  0000000000005060  00004060  2**3
 26 .bss          00000118  0000000000005080  0000000000005080  00004078  2**6
$ ls -l bss_data
-rwxr-xr-x 1 root root 4210392 Aug 15 12:43 bss_data
$ size bss_data
   text     data      bss      dec      hex filename
   1469  4194952  4194336  8390757   800865 bss_data
$ size lib_plain lib_gc
   text    data     bss     dec     hex filename
   1301     560       8    1869     74d lib_plain
   1267     552       8    1827     723 lib_gc
$ nm lib_plain | grep -E "used_fn|never_called"
000000000000111f T _Z12never_calledv
0000000000001119 T _Z7used_fnv
$ nm lib_gc | grep -E "used_fn|never_called"
0000000000001119 T _Z7used_fnv
```

## 步骤 5：调试实战 {#lab-5}

**思路**：gdb 的 `break` 是控制、`print` 是观测、`bt` 结合符号表是映射——三个能力一次会话做完；ASan（教材外补充）报告把「变量、行号、越界方向」全部点名。

1. gdb 会话：断在 `an::count_text`，`bt` 显示 `main` 在第 17 行调用它，`print text.size()` 得到 32。→ 知识点：[MSVC 调试原理](../msvc-debugging-internals.md)「从什么是调试讲起」一节（三能力）
2. 体积对比：`-g` 构建 115848 字节、7 个 `debug_*` 段；`-O2` 无 `-g` 22888 字节、0 个——调试信息住在磁盘镜像的 debug 段里，不进运行时代码。→ 知识点：[编译器选项](../02-compiler-options.md)「输出管理与调试信息」一节
3. ASan：`WRITE of size 6`、点名 `token`（`[32, 36)` 越界）、定位 `buggy.cpp:7` 的 `strcpy`、退出码 1。→ 知识点：[MSVC 调试原理](../msvc-debugging-internals.md)「堆栈损坏」一节（sanitizer 让损坏当场现形）

**验证输出**：

```text
$ gdb -q -batch -x lab5_cmds.txt ./analyzer_g
Breakpoint 1, an::count_text (text="hello world\nmodern c++ vol7\nbye\n") at src/analyzer_core.cpp:8
#0  an::count_text (text="hello world\nmodern c++ vol7\nbye\n") at src/analyzer_core.cpp:8
#1  0x0000555555556358 in main (argc=2, argv=0x7fffffffdf48) at src/main.cpp:17
$1 = 32
$ ls -l analyzer_g analyzer_nog
-rwxr-xr-x 1 root root 115848 Aug 15 12:43 analyzer_g
-rwxr-xr-x 1 root root  22888 Aug 15 12:43 analyzer_nog
$ readelf -S analyzer_g | grep -c "debug_"
7
$ readelf -S analyzer_nog | grep -c "debug_"
0
$ g++ -std=c++17 -g -fsanitize=address buggy.cpp -o buggy_asan
$ ./buggy_asan HELLO; echo "exit=$?"
=================================================================
==614==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x6c48e44f0024 ...
WRITE of size 6 at 0x6c48e44f0024 thread T0
    #1 0x572cd24b3258 in parse_line(char*) /tmp/cpp-v7-lab15/lab5/buggy.cpp:7
    #2 0x572cd24b3317 in main /tmp/cpp-v7-lab15/lab5/buggy.cpp:16
  This frame has 1 object(s):
    [32, 36) 'token' (line 6) <== Memory access at offset 36 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow /tmp/cpp-v7-lab15/lab5/buggy.cpp:7 in parse_line(char*)
exit=1
```

## 步骤 6：模块登场 {#lab-6}

**思路**：这一部里藏着一个真坑——模块接口直接 `#include <string>` 时，GCC 的 `-fmodules`（教材外补充：GCC 侧开启 C++20 模块的开关，教材的 MSVC 侧由 IDE 自动扫描）会在消费方重定义标准库实体（`std::_Index_tuple`、`_IO_FILE` 冲突）。标准姿势是把标准头放进全局模块片段（`module;` 开头）——**先 include 后 import 完全干净，先 import 后 include 才撞车**（顺序敏感，本机实测）；所以本实验的接口**只用核心语言类型**（`const char*` + `unsigned long long`），消费方随便 include 标准头都不冲突。要用 `std::string` 出模块接口，正路是 `import std;`（MSVC/VS2026 开箱即用）或头单元（`g++ -xc++-system-header string` 预编译后 `import <string>;`），这是本机实测结论，不是编的。

1. 先把「带 `std::string` 的版本」跑炸一次，真实报错如下（节选）。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)（模块与头文件的边界）
2. `analyze.cppm` 最终版：接口只有核心语言类型，构建日志 6 步（Scan → dyndep → 两个对象 → 链接）。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)「最小可运行示例」一节、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)（CMake 承担了 MSVC 里 IDE 的自动扫描职责）
3. 触碰证据链：`touch main.cpp` 只重编 main；`touch analyze.cppm` 重编模块 + main；零改动 `no work to do`。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)「为什么要用模块」一节（BMI 缓存）

**验证输出**：

```text
$ cmake -S . -B build -G Ninja && cmake --build build
[1/6] Scanning /tmp/cpp-v7-lab6c/lab6/analyze.cppm for CXX dependencies
[2/6] Scanning /tmp/cpp-v7-lab6c/lab6/main.cpp for CXX dependencies
[3/6] Generating CXX dyndep file CMakeFiles/analyzer.dir/CXX.dd
[4/6] Building CXX object CMakeFiles/analyzer.dir/analyze.cppm.o
[5/6] Building CXX object CMakeFiles/analyzer.dir/main.cpp.o
[6/6] Linking CXX executable analyzer
$ ./build/analyzer sample.txt
bytes=32 lines=3 words=6
$ touch main.cpp && ninja -C build -v | grep -oE "\-c [^ ]*\.(cpp|cppm)"
-c /tmp/cpp-v7-lab6c/lab6/main.cpp
$ touch analyze.cppm && ninja -C build -v | grep -oE "\-c [^ ]*\.(cpp|cppm)"
-c /tmp/cpp-v7-lab6c/lab6/analyze.cppm
-c /tmp/cpp-v7-lab6c/lab6/main.cpp
$ ninja -C build
ninja: no work to do.
```

「接口用 `std::string`」的失败版本，真实报错节选：

```text
In file included from /usr/include/c++/16/bits/stl_algobase.h:63:
/usr/include/c++/16/bits/stl_pair.h:102:12: error: conflicting declaration of 'struct std::_Index_tuple<_Indexes>' in module 'analyze'
/usr/include/c++/16/type_traits:4372:39: note: previously declared in global module
In file included from /usr/include/stdio.h:48:
/usr/include/bits/types/struct_FILE.h:37:8: error: conflicting declaration of 'struct _IO_FILE' in global module
```

## 附加挑战（L5）：矩阵发布攻坚 {#lab-l5}

**思路**：8 个组合每一格都是独立的「配置 + 构建 + 检验」；`-Werror` 门禁让「零警告」从口号变成可执行约束；埋雷实验证明门禁真会拦。

1. 矩阵脚本核心：`{g++, clang++} × {Debug, Release} × {Ninja, "Unix Makefiles"}`，每格独立目录、独立配置、`size` 取 text。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「基于构建目录的多目标方案」一节、[编译器选项](../02-compiler-options.md)「CMake 中的最佳实践配置」一节（INTERFACE 警告库）
2. 8/8 全绿：g++ Debug text=12088、Release 6809；clang++ Debug 13137、Release 5983——`text` 大小取决于 Debug/Release 两套优化旗标生成的机器码，调试信息不进 `text`（它住在 `.debug_*` 段里，步骤 5 有实拍），具体大小关系取决于代码与编译器，本机没有普适机制。→ 知识点：[编译器选项](../02-compiler-options.md)「优化等级」与「输出管理与调试信息」两节
3. 埋雷：`int unused_here = 7;` 让 `-Werror=unused-variable` 当场拦截，构建退出码 1。→ 知识点：同上（`-Werror` 门禁）

**验证输出**：

```text
$ for CC in g++ clang++; do ... done        # 矩阵脚本(缩写版;完整脚本是学习者要自己写的交付物)
cc      config  generator          text  result
g++     Debug   Ninja             12088  build OK
g++     Debug   Unix Makefiles    12088  build OK
g++     Release Ninja              6809  build OK
g++     Release Unix Makefiles     6809  build OK
clang++ Debug   Ninja             13137  build OK
clang++ Debug   Unix Makefiles    13137  build OK
clang++ Release Ninja              5983  build OK
clang++ Release Unix Makefiles     5983  build OK
$ ./m-g++-debug-ninja/analyzer sample.txt      # 每格产物运行输出一致
bytes=32 lines=3 words=6
$ cmake --build bad/m; echo "exit=$?"           # 埋雷工程:门禁拦截
/tmp/cpp-v7-lab6b/matrix/bad/src/main.cpp:8:9: error: unused variable 'unused_here' [-Werror=unused-variable]
exit=1
```

换编译器必须换构建目录的原因：编译器的选择被写进 `CMakeCache.txt`，混用一个目录会让 CMake 报「cache 需要删除」并重配（Homework 7.6-B 有实拍），更别说产物和 flag 的残留——矩阵脚本里每格独立目录就是最干净的做法。
