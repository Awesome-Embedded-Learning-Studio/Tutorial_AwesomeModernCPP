---
title: "卷 7 课后练习参考答案（Homework）"
description: "软件工程实践卷课后练习的逐题详细解答：19 道题每题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出（g++ 16.1.1 / clang++ 22.1.8 / CMake 4.4.2 / WSL Arch 实跑，全部构建在 /tmp 独立目录完成）。"
chapter: 7
order: 2
tags: [host, intermediate, cpp-modern, CMake, 工程实践]
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 45
prerequisites:
  - "卷 7 课后练习（Homework）"
related:
  - "卷 7 全部章节（第 7.1~7.8 章）"
---

# 卷 7 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1、clang++ 22.1.8、CMake 4.4.2、Ninja 1.13.2、gdb 17.2）下真实运行得到；全部 CMake 构建都在 `/tmp` 独立目录里做。耗时类的数值每台机器不同，趋势才是你要对上的东西。

## 7.1-A {#hw-7-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-7-1-a)

**思路**：`-S` 指定源码树、`-B` 指定构建树，两者分离就是 out-of-source 的根基；「缓存」是 CMake 记住工具链与选项的数据库。

1. 建工程、配置、构建、运行，输出贴在验证块。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「CMake 基本概念」一节（Source Tree / Build Tree / Generator / 变量与缓存）
2. `-S` 是 `--source`、`-B` 是 `--build` 的缩写；构建产物全在 `build/`，源码树一个字节没动——这就是 out-of-source。→ 知识点：同上（推荐 out-of-source 构建的原因）
3. 生成器决定「CMake 生成什么样的构建文件」（Makefile / build.ninja / VS 工程）；交叉编译的 toolchain 文件必须第一次配置就传，因为之后它被写进 `CMakeCache.txt` 缓存——CMake 之后每次都从缓存读工具链，不再理会命令行。→ 知识点：同上（Generator / 变量和缓存）、「使用 Toolchain 文件」一节（第一次必须指定）

**验证输出**：

```text
$ cmake -S . -B build
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (2.1s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cpp-v7-hw12/hello/build
$ cmake --build build
[ 50%] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[100%] Linking CXX executable hello
[100%] Built target hello
$ ./build/hello
Hello, vol7!
```

## 7.1-B {#hw-7-1-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-1-b)

**思路**：裸的 target 名不是 target 时，CMake 把它当「库名」转成 `-l<name>` 传给链接器——所以配置能过、链接才炸。修复用 target 语义；`PUBLIC`/`PRIVATE` 决定编译定义和 include 目录沿依赖图传播多远。

1. 坏版本配置**成功**、构建**失败**：`greeter_typo` 不是 target，被当成库名，链接器报 `cannot find -lgreeter_typo`。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「CMake 基本概念」一节（Target 概念）
2. 修复：include 目录挂到 `greeter_static` 上并标 `PUBLIC`，app 链接它就能看到头文件；构建运行成功。→ 知识点：同上「平台抽象层（HAL）设计」一节（`target_include_directories` / `target_link_libraries` 的传播）
3. 泄漏实验：`PUBLIC` 的定义传到了 app（可见），`PRIVATE` 的没有（不可见）——`PRIVATE` 是「自己编译用、使用方看不到」。→ 知识点：同上（PRIVATE/PUBLIC/INTERFACE 三档传播语义）

**验证输出**：

```text
$ cmake -S . -B build && cmake --build build      # 坏版本:配置过、构建炸
[ 25%] Building CXX object CMakeFiles/greeter_static.dir/src/greeter.cpp.o
[ 50%] Linking CXX static library libgreeter_static.a
[ 50%] Built target greeter_static
[ 75%] Building CXX object CMakeFiles/app.dir/src/app.cpp.o
[100%] Linking CXX executable app
/usr/bin/ld: cannot find -lgreeter_typo: No such file or directory
collect2: error: ld returned 1 exit status
make[2]: *** [CMakeFiles/app.dir/build.make:101: app] Error 1
make: *** [Makefile:91: all] Error 2
$ cmake -S . -B build-fixed && cmake --build build-fixed    # 修复后
[ 75%] Building CXX object CMakeFiles/app.dir/src/app.cpp.o
[100%] Linking CXX executable app
[100%] Built target app
$ ./build-fixed/app
Hello, vol7!
$ ./build/leak        # PUBLIC/PRIVATE 泄漏实验
PRIVATE def not visible: OK
kCoreValue=42
```

## 7.2-A {#hw-7-2-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-7-2-a)

**思路**：`-Wall -Wextra` 只报警告不拦构建，`-Werror` 把警告升级成错误——CI 里用它才能把「零警告」钉进提交门槛；`-D` 在命令行定义宏，进不去宏分支的代码根本不会参与编译。

1. `-Wall -Wextra` 下三条警告：`-Wparentheses`（赋值当条件）、两条 `-Wunused-variable`。→ 知识点：[编译器选项](../02-compiler-options.md)「警告治理：`-W` 系列」一节
2. 加 `-Werror` 后同样的三处变成 error，构建退出码 1。→ 知识点：同上（`-Werror` 在 CI 的推荐用法）
3. `-DDEBUG_LEVEL=2` 编译运行输出 `DEBUG_LEVEL=2`；不带 `-D` 走 `#else` 分支。宏在**预处理期**替换，没定义的宏分支连编译都不参与——这就是为什么宏堆多了代码路径难测。→ 知识点：同上「预处理器与宏定义」一节（`-D` 与过度依赖宏的警告）
4. 修复版：`x == 5`、删掉未用变量，`-Werror -DDEBUG_LEVEL=3` 零警告通过。→ 知识点：同上

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra warn1.cpp -o warn1
warn1.cpp:12:11: warning: suggest parentheses around assignment used as truth value [-Wparentheses]
   12 |     if (x = 5) {           // 赋值写成 == ,故意埋雷
warn1.cpp:5:9: warning: unused variable 'level' [-Wunused-variable]
warn1.cpp:15:9: warning: unused variable 'unused' [-Wunused-variable]
$ g++ -std=c++17 -Wall -Wextra -Werror warn1.cpp -o warn1b; echo "exit=$?"
warn1.cpp:12:11: error: suggest parentheses around assignment used as truth value [-Werror=parentheses]
warn1.cpp:5:9: error: unused variable 'level' [-Werror=unused-variable]
warn1.cpp:15:9: error: unused variable 'unused' [-Werror=unused-variable]
cc1plus: all warnings being treated as errors
exit=1
$ g++ -std=c++17 -Wall -Wextra -DDEBUG_LEVEL=2 warn1.cpp -o warn1d && ./warn1d
DEBUG_LEVEL=2
x is five
$ ./warn1
no DEBUG_LEVEL defined
x is five
$ g++ -std=c++17 -Wall -Wextra -Werror -DDEBUG_LEVEL=3 warn1_fixed.cpp -o warn1f && ./warn1f
DEBUG_LEVEL=3
x is five
```

## 7.2-B {#hw-7-2-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-2-b)

**思路**：本机最震撼的不是「-O2 快多少倍」，而是 `-O2`/`-Os` 直接把循环**从二进制里抹掉了**——编译器认出了等差数列求和公式，在编译期算完，运行耗时 0 ms。这比任何「快 N 倍」都更能说明优化不是「把代码翻译得更快」而是「重新推导你的意图」。

1. 耗时实测：`-O0` 约 800 ms，`-O2`/`-Os` 为 0.0 ms——循环被常量折叠（`0..N-1` 的和是 $\frac{N(N-1)}{2}$），程序只剩一句赋值。→ 知识点：[编译器选项](../02-compiler-options.md)「优化等级」一节（`-O0`/`-O2`/`-Os` 的行为差异）
2. `size` 对比：`-O0` text 2940，`-O2`/`-Os` 都降到 1679——`chrono` 与 `printf` 的调用代码被精简掉，且 `-O2` 与 `-Os` 在这个小例子上恰好同尺寸。→ 知识点：同上（`-Os` 是嵌入式发布的默认选择）
3. `-Wdouble-promotion`：$x \times 0.5$ 里 0.5 是 `double`，`float x` 被隐式提升成 `double` 再截断回 `float`——每次运算多一次「提升 + 截断」。→ 知识点：同上「`-Wdouble-promotion`」一节（无硬件双精度 FPU 时性能暴跌）
4. gc-sections：普通链接 text=1305 且 `nm` 里两个函数都在；加 `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections` 后 text 降到 1263，`unused_fn` 从符号表消失。→ 知识点：同上「垃圾回收不用的代码」一节、[链接器与链接脚本](../03-linker-and-linker-scripts.md)「函数级链接优化」一节

**验证输出**：

```text
$ for o in 0 2 s; do g++ -std=c++17 -O$o hotloop.cpp -o hotloop_O$o; done
$ size hotloop_O0 hotloop_O2 hotloop_Os
   text    data     bss     dec     hex filename
   2940     648      16    3604     e14 hotloop_O0
   1679     640      16    2335     91f hotloop_O2
   1679     640      16    2335     91f hotloop_Os
$ for o in 0 2 s; do echo "[-O$o]"; for r in 1 2 3; do ./hotloop_O$o; done; done
[-O0]
elapsed=791.2 ms (sink=124999999750000000)
elapsed=795.0 ms (sink=124999999750000000)
elapsed=808.6 ms (sink=124999999750000000)
[-O2]
elapsed=0.0 ms (sink=124999999750000000)
elapsed=0.0 ms (sink=124999999750000000)
elapsed=0.0 ms (sink=124999999750000000)
[-Os]
elapsed=0.0 ms (sink=124999999750000000)
elapsed=0.0 ms (sink=124999999750000000)
elapsed=0.0 ms (sink=124999999750000000)
$ g++ -std=c++17 -Wall -Wextra -Wdouble-promotion promote.cpp -o promote
promote.cpp:5:14: warning: implicit conversion from 'float' to 'double' to match other operand of binary expression [-Wdouble-promotion]
    5 |     return x * 0.5;   // 0.5 是 double,x 被提升成 double 再截断回 float
$ size gc_plain gc_stripped
   text    data     bss     dec     hex filename
   1305     560       8    1873     751 gc_plain
   1263     552       8    1823     71f gc_stripped
$ nm gc_plain | grep -E "used_fn|unused_fn"
0000000000001130 T _Z7used_fnv
0000000000001136 T _Z9unused_fnv
$ nm gc_stripped | grep -E "used_fn|unused_fn"
0000000000001130 T _Z7used_fnv
```

## 7.3-A {#hw-7-3-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-3-a)

**思路**：`nm` 就是链接器的「进货单」：大写是拿出来卖的强符号、小写是本文件私藏、`U` 是等别人供货的引用。

1. `T`＝代码段里定义的全局函数，`t`＝本地符号（`static` 函数），`D`＝已初始化数据，`U`＝未定义、等链接器解析。`local_helper` 因为 `static` 是小写 `t`，链接后仍是 `t`——它被保留下但不在全局符号表里「卖」。→ 知识点：[链接器与链接脚本](../03-linker-and-linker-scripts.md)「链接器的四大核心任务」一节（符号解析）
2. 链接后 `nm prog`：`bump_counter` 是 `T`、`local_helper` 是 `t`、`g_counter` 是 `D` 且有了地址 `0x4020`——这就是「地址分配」的实锤：`.o` 里全是 0，链接后才填上。→ 知识点：同上（地址分配与段合并）
3. `undefined reference to 'launch_missiles()'` 是链接器在说：有个 `U` 符号全世界没人定义。注意编译能过（声明足够），**链接**才炸——阶段别搞错。→ 知识点：同上（符号解析失败）

**验证输出**：

```text
$ nm libpart.o
0000000000000000 T _Z12bump_counterv
000000000000001b t _ZL12local_helperv
0000000000000000 D g_counter
0000000000000000 D g_version
$ nm -C libpart.o
0000000000000000 T bump_counter()
000000000000001b t local_helper()
0000000000000000 D g_counter
0000000000000000 D g_version
$ nm usepart.o | grep -vE "std::|ios_base|ostream|Unwind|personality|stack_chk|DW.ref|operator"
                 U _Z12bump_counterv
                 U g_counter
0000000000000000 T main
$ g++ libpart.o usepart.o -o prog && ./prog
before: 10
after:  11
$ nm prog | grep -E "bump_counter|g_counter|local_helper"
0000000000001149 T _Z12bump_counterv
0000000000001164 t _ZL12local_helperv
0000000000004020 D g_counter
$ g++ badcall.o -o badprog; echo "exit=$?"
/usr/bin/ld: badcall.o: in function `main':
badcall.cpp:(.text+0x5): undefined reference to `launch_missiles()'
collect2: error: ld returned 1 exit status
exit=1
```

## 7.3-B {#hw-7-3-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-3-b)

**思路**：跨翻译单元的全局对象构造顺序由链接器决定的 `.init_array` 顺序说了算——标准不保证，所以「预测输出」这一步很多人会翻车；`.bss` 不占文件空间是「零值不需要存」的直接后果。

1. 链接顺序 `a.cpp b.cpp main.cpp` 输出先 A 后 B；`b.cpp a.cpp main.cpp` 先 B 后 A——同一份源码、顺序翻转，标准允许。这就是 fiasco：对象 A 的构造函数用到 B 时，B 可能还没出生。→ 知识点：[链接器与链接脚本](../03-linker-and-linker-scripts.md)「全局对象构造顺序」一节（静态初始化顺序问题）
2. Meyers 单例：`static Logger inst;` 首次进入函数才构造，第二次调用不再构造——输出里 `Logger constructed (first use)` 出现在 `main entered` 之后、只出现一次。→ 知识点：同上（Meyers 单例是推荐解）
3. `.bss` 实验：文件总大小 8.4MB、`data` 段 8389256 字节 ≈ 8MB——`big_data` 的非零初始值被存进了文件；`big_zero` 在 `.bss`（8388640 字节）但一个字节都没占文件空间。链接脚本/启动代码负责「运行时把 `.bss` 清零」，而不是把 8MB 零塞进镜像。→ 知识点：同上「不同段的作用」一节（`.bss` 不占用 FLASH 空间）

**验证输出**：

```text
$ g++ -std=c++17 -O0 a.cpp b.cpp main.cpp -o order_ab && ./order_ab
A constructed
B constructed
$ g++ -std=c++17 -O0 b.cpp a.cpp main.cpp -o order_ba && ./order_ba
B constructed
A constructed
$ g++ -std=c++17 meyers.cpp -o meyers && ./meyers
main entered
Logger constructed (first use)
done
$ g++ -std=c++17 -O0 bss_data.cpp -o bss_data
$ ls -l bss_data
-rwxr-xr-x 1 root root 8404696 Aug 15 12:36 bss_data
$ size bss_data
   text     data      bss      dec      hex filename
   1469  8389256  8388640 16779365  1000865 bss_data
```

## 7.4-A {#hw-7-4-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-7-4-a)

**思路**：骨架题的价值在「头文件是契约」——接口先立起来，零警告编译过，后面实现怎么写都跑不出这个框。

1. 完整骨架见验证块后的代码要点，`-Wall -Wextra -c` 零警告（成功无输出即通过）。→ 知识点：[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)「接口设计」一节
2. `explicit` 防的是 `FileCopier copier = 4096;` 这种隐式转换——构造函数带默认参数后它就是个「隐式转换函数」，哪天函数签名是 `void f(const FileCopier&)` 就会有人写出 `f(4096)` 这种魔幻调用。→ 知识点：同上（`explicit` 是好习惯）
3. `vector<char>` 自动管理内存（RAII，异常安全），`data()` 给连续缓冲；`new char[]` 要手动 `delete[]`，异常路径上必漏。→ 知识点：同上「动态数组」一节
4. `const std::string&` 避免按值传的整串拷贝，调用者传入的临时量/左值都适用。→ 知识点：同上（接口设计：`copy` 的参数）

**关键代码**（完整版就是题面骨架）：

```cpp
// fcopy.h
#pragma once

#include <cstddef>
#include <string>

class FileCopier
{
public:
    explicit FileCopier(std::size_t chunk_size = 8 * 1024);
    bool copy(const std::string& src_path, const std::string& dst_path);
    void set_chunk_size(std::size_t size)
    {
        chunk_size_ = size;
    }

private:
    std::size_t chunk_size_;
};
```

```cpp
// fcopy_skel.cpp
#include "fcopy.h"

FileCopier::FileCopier(std::size_t chunk_size)
    : chunk_size_(chunk_size)
{
}

bool FileCopier::copy(const std::string& /*src_path*/,
                      const std::string& /*dst_path*/)
{
    return false;   // 骨架:下一题再填
}
```

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra -c fcopy_skel.cpp -o fcopy_skel.o; echo "exit=$?"
exit=0
```

## 7.4-B {#hw-7-4-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-4-b)

**思路**：四个用例里三个死在打开文件之前，唯一「出人意料」的是目录——`exists` 对它返回 `true`，于是必须靠 `file_size` 的异常来兜，这也正是教材先捕获 `filesystem_error` 的原因。

1. 检查链：`exists` → `file_size` → 打开输入流 → 打开输出流 → 分配缓冲。→ 知识点：[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)「前置检查」与「打开文件」两节
2. 用例③验证「目标目录不存在」在 `ofstream` 构造时失败（`!out` 分支），用例④验证 `file_size` 对目录抛 `filesystem_error`、走 catch 分支打印 `cannot get file size: Is a directory`。→ 知识点：同上（`try-catch` 的错误处理策略）

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra precheck.cpp pre_main.cpp -o precheck
$ ./precheck exists.txt out.txt; echo "exit=$?"
prepared: src=exists.txt size=3 chunk=4096
exit=0
$ ./precheck nope.txt out.txt; echo "exit=$?"
Source file does not exist: nope.txt
exit=1
$ ./precheck exists.txt /no_such_dir/out.txt; echo "exit=$?"
Failed to open destination file for writing: /no_such_dir/out.txt
exit=1
$ ./precheck . out.txt; echo "exit=$?"
Filesystem error: filesystem error: cannot get file size: Is a directory [.]
exit=1
```

## 7.5-A {#hw-7-5-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-5-a)

**思路**：`read` 不一定读满，`gcount` 报告真实读到的字节数；循环条件用流状态而不是 `eof()`，因为 `eof()` 只有「已经撞到文件尾」才为真，错误状态它看不见。

1. 主循环：`while (in)` + `read` + `gcount` + 写实际字节数 + 每次写后查流状态。→ 知识点：[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)「核心读写循环」一节
2. 100003 字节 = 24 块整 4096 + 最后一块 1699 字节，共 25 块；`md5sum` 前后一致。→ 知识点：同上「read 和 gcount 的配合使用」一节（最后一块只写实际字节数）
3. 空文件：第一次 `read` 就读到 0 字节、`gcount()` 返回 0，`break`——循环体一次没进，`copied 0 bytes in 0 blocks`。→ 知识点：同上（`read_bytes <= 0` 的保险）
4. 循环后 `flush`/`close` + 大小校验，防「看起来成功」的静默损坏。→ 知识点：同上「收尾工作」一节

**验证输出**：

```text
$ g++ -std=c++17 -Wall -Wextra fcopy.cpp pre_main.cpp -o fcopy
$ dd if=/dev/urandom of=odd.bin bs=100003 count=1 2>/dev/null
$ ./fcopy odd.bin odd_copy.bin
copied 100003 bytes in 25 blocks (chunk=4096)
$ ./fcopy empty.bin empty_copy.bin
copied 0 bytes in 0 blocks (chunk=4096)
$ ls -l odd.bin odd_copy.bin empty_copy.bin
-rw-r--r-- 1 root root      0 Aug 15 12:40 empty_copy.bin
-rw-r--r-- 1 root root 100003 Aug 15 12:40 odd.bin
-rw-r--r-- 1 root root 100003 Aug 15 12:40 odd_copy.bin
$ md5sum odd.bin odd_copy.bin
b45191db7fccf447f10bafba65644bb7  odd.bin
b45191db7fccf447f10bafba65644bb7  odd_copy.bin
$ ./fcopy nope.bin x.bin; echo "exit=$?"
Source file does not exist: nope.bin
exit=1
```

## 7.5-B {#hw-7-5-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-5-b)

**思路**：块大小换的是「系统调用次数」：4KB 块要 16384 次 `read`+`write`，1MB 块只要 64 次。每次系统调用都要进出内核、都要拷贝，频率高了就亏。

1. 基准程序在 `copy` 前后取 `steady_clock`，换算 MB/s。→ 知识点：[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)「时间和速度计算」一节（`steady_clock` 测间隔）、[编译器选项](../02-compiler-options.md)（`-O2` 基准构建）
2. 本机实测：4KB 约 1759~2441 MB/s，64KB 约 2856~3113 MB/s，1MB 约 3330~3437 MB/s——块越大越快，但 4KB 档比 1MB 档慢约 1.4~2 倍，而 64KB 再往上只剩约 1.1~1.2 倍的收益，明显收敛。→ 知识点：[文件拷贝器（上）](../01-file-copier-requirements-and-framework.md)「分块读写」一节（块大小的经验讨论）
3. 再大到 16MB：内存压力大、缓冲区失效、且大块拷贝中途失败时重传代价高；8KB 默认值是「系统调用开销」与「内存/粒度」的折中。→ 知识点：同上（默认 8KB 保守）

**验证输出**（64MB 文件，每档 3 次）：

```text
$ g++ -std=c++17 -O2 -Wall -Wextra fcopy.cpp bench.cpp -o bench
$ dd if=/dev/zero of=big64.bin bs=1M count=64 2>/dev/null
$ for cs in 4096 65536 1048576; do for r in 1 2 3; do ./bench big64.bin big_copy.bin $cs; done; done
chunk=4096 bytes: 36.3832 ms, 1759.05 MB/s
chunk=4096 bytes: 26.2167 ms, 2441.19 MB/s
chunk=4096 bytes: 29.0976 ms, 2199.49 MB/s
chunk=65536 bytes: 20.8275 ms, 3072.85 MB/s
chunk=65536 bytes: 22.4043 ms, 2856.6 MB/s
chunk=65536 bytes: 20.5564 ms, 3113.39 MB/s
chunk=1048576 bytes: 18.965 ms, 3374.63 MB/s
chunk=1048576 bytes: 18.6191 ms, 3437.34 MB/s
chunk=1048576 bytes: 19.2138 ms, 3330.95 MB/s
```

## 7.6-A {#hw-7-6-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-6-a)

**思路**：生成器只影响「CMake 生成什么构建文件」，不影响你的程序输出——同一个编译器、同一份代码，产物应当逐字节语义一致。

1. 环境自报程序打印 `__cplusplus=201703`（`-std=c++17` 下就是这个值）、`GCC 16.1.1`、`libstdc++`。→ 知识点：[WSL 开发 C++](../cpp-development-on-wsl.md)「首次进入 WSL」一节（gcc/clang 工具链）
2. `-G` 影响的是 CMake 三阶段里的**生成**阶段：配置（读 CMakeLists、算依赖）→ 生成（按生成器写出 Makefile 或 build.ninja）→ 构建（make/ninja 干活）。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「CMake 基本概念」一节（Generator）
3. 两次构建的程序输出完全一样；构建日志的差异在于驱动工具（`make` vs `ninja`）——教材那句 `cmake .. -G "Ninja"` 就是选生成器。→ 知识点：[WSL 开发 C++](../cpp-development-on-wsl.md)「创建一个最小 CMake + C++ 项目」一节

**验证输出**：

```text
$ cmake -S . -B b-make -G "Unix Makefiles" && cmake --build b-make && ./b-make/gen_demo
__cplusplus=201703
compiler=GCC 16.1.1
stdlib=libstdc++
$ cmake -S . -B b-ninja -G Ninja && cmake --build b-ninja && ./b-ninja/gen_demo
__cplusplus=201703
compiler=GCC 16.1.1
stdlib=libstdc++
```

## 7.6-B {#hw-7-6-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-6-b)

**思路**：clang 为了兼容 GCC 生态定义了 `__GNUC__` 系列宏，值定格在 4.2.1——识别编译器要先看 `__clang__` 再看 `__GNUC__`，否则你会把 clang 认成「GCC 4.2」。

1. clang 构建后程序输出 `compiler=GCC 4.2.1` **和** `compiler=Clang 22.1` 两行——`__GNUC__` 是兼容宏，`__clang__` 才是真身。→ 知识点：[WSL 开发 C++](../cpp-development-on-wsl.md)（推荐安装 clang、注意与 ms-vscode.cpptools/clangd 的搭配）
2. 标准库身份：clang 在这个发行版上默认链接 libstdc++，所以输出 `stdlib=libstdc++` 而不是 `libc++`。→ 知识点：[编译器选项](../02-compiler-options.md)「语言标准控制」一节（预定义宏是编译器实现细节）
3. 同一构建目录换编译器：CMake 检测到缓存变量变化，提示 `You have changed variables that require your cache to be deleted` 并重跑配置——工程上不推荐混用一个构建目录，换 kit 就删目录重建。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「使用 Toolchain 文件」一节（更换配置必须删构建目录）

**验证输出**：

```text
$ cmake -S . -B b-clang -G Ninja -DCMAKE_CXX_COMPILER=clang++ && cmake --build b-clang && ./b-clang/gen_demo
__cplusplus=201703
compiler=GCC 4.2.1
compiler=Clang 22.1
stdlib=libstdc++
$ cmake -S . -B b-clang -G Ninja -DCMAKE_CXX_COMPILER=g++     # 同一目录换 kit
You have changed variables that require your cache to be deleted.
Configure will be re-run and you may have to reset some variables.
The following variables have changed:
CMAKE_CXX_COMPILER= g++
-- The CXX compiler identification is GNU 16.1.1
-- Configuring done (1.8s)
```

## 7.7-A {#hw-7-7-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-7-7-a)

**思路**：模块把「头文件文本拼接」换成了「预编译接口 + 按需导入」。三个关键词分工：`export module` 声明我是模块、`export` 决定哪些名字出口、`import` 消费别人导出的接口。

1. BMI（Binary Module Interface）是 MSVC 的模块编译产物（GCC 侧对应 `.gcm`/模块对象），VS2026 用它做增量编译和跨模块依赖分析——改一个模块只重编依赖它的模块，不再整文件级联重编。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)「为什么要用模块」一节
2. `.ixx` 是 MSVC 社区惯例的模块接口扩展名，`.cppm` 是 GCC/Clang 常用的等价物；IDE 按扩展名识别模块源。→ 知识点：同上「最小可运行示例」一节的说明
3. 真跑（`-fmodules` 属教材外补充，GCC 侧开启 C++20 模块的开关；教材的 MSVC 侧由 IDE 自动处理）：g++ 16 需要显式 `-fmodules`（C++20 模块在本机 gcc 上还不是默认开启的），`-c` 先把模块编成目标文件再链接。→ 知识点：同上（VS2026 上 IDE 自动处理这些次序；`/std:c++20` 默认开启）

**验证输出**：

```text
$ g++ -std=c++20 -fmodules -Wall -Wextra -c math.cppm -o math.o
$ g++ -std=c++20 -fmodules -Wall -Wextra main.cpp math.o -o math_app
$ ./math_app
add(1,2)=3
Point(1, 2)
```

## 7.7-B {#hw-7-7-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-7-b)

**思路**：三个触碰实验构成 BMI 缓存的完整证据链——「依赖扫描」知道谁 import 谁，「按需重编」只在依赖图上做最小传播。

1. CMake 4 的 `FILE_SET CXX_MODULES` + `-fmodules`：构建日志先 Scan 三个文件、生成 dyndep，再按依赖序编模块。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)（IDE/MSBuild 的自动扫描在 GCC 侧由 CMake 承担）、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)（target 语义）
2. `touch main.cpp` → 只有 `main.cpp.o` 重编，两个模块对象复用。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)「为什么要用模块」一节（增量编译分析到二进制 ABI 层次）
3. `touch core.cppm` → `core`、`report`、`main` **全部**重编：接口模块变了，整条 import 链都要重新消费它的接口。→ 知识点：同上（模块间依赖的正确传播）
4. `touch report.cppm` → `report` 和 `main` 重编，`core.cppm.o` 原样复用——BMI 缓存让「没被影响的下游」零开销。→ 知识点：同上（BMI 缓存编译产物）
5. 零改动构建：`ninja: no work to do.`——时间戳 + 依赖图都没变。→ 知识点：[编译器选项](../02-compiler-options.md)「依赖生成：`-M`, `-MMD`」一节（构建系统怎么知道该重编谁）

**验证输出**：

```text
$ cmake -S . -B build -G Ninja && cmake --build build
[1/8] Scanning /tmp/cpp-v7-hw77b/modchain/core.cppm for CXX dependencies
[2/8] Scanning /tmp/cpp-v7-hw77b/modchain/report.cppm for CXX dependencies
[3/8] Scanning /tmp/cpp-v7-hw77b/modchain/main.cpp for CXX dependencies
[4/8] Generating CXX dyndep file CMakeFiles/modchain.dir/CXX.dd
[5/8] Building CXX object CMakeFiles/modchain.dir/core.cppm.o
[6/8] Building CXX object CMakeFiles/modchain.dir/report.cppm.o
[7/8] Building CXX object CMakeFiles/modchain.dir/main.cpp.o
[8/8] Linking CXX executable modchain
$ ./build/modchain
50 -> 50
$ touch main.cpp && ninja -C build -v | grep -oE "\-c [^ ]*\.(cpp|cppm)"
-c /tmp/cpp-v7-hw77b/modchain/main.cpp
$ touch core.cppm && ninja -C build -v | grep -oE "\-c [^ ]*\.(cpp|cppm)"
-c /tmp/cpp-v7-hw77b/modchain/core.cppm
-c /tmp/cpp-v7-hw77b/modchain/report.cppm
-c /tmp/cpp-v7-hw77b/modchain/main.cpp
$ touch report.cppm && ninja -C build -v | grep -oE "\-c [^ ]*\.(cpp|cppm)"
-c /tmp/cpp-v7-hw77b/modchain/report.cppm
-c /tmp/cpp-v7-hw77b/modchain/main.cpp
$ ninja -C build
ninja: no work to do.
```

（为省版面，`ninja -v` 的完整命令行只保留了 `-c <源文件>` 部分；三次实验里其余参数一致。）

## 7.8-A {#hw-7-8-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-7-8-a)

**思路**：调试三能力在 GDB 里各有一个具体动作——`print` 是观测、`break/next` 是控制、`list`/符号化是映射；PDB（ELF 的 `.debug_*` 段）是映射的地图，地图不在 Flash 里。

1. 观测=查看变量/内存/线程，控制=断点/单步/改值，映射=把地址翻译回 `文件:行号`。→ 知识点：[MSVC 调试原理](../msvc-debugging-internals.md)「从什么是调试讲起」一节
2. 分工：IDE 只发指令、DE（调试引擎）解析表达式读 PDB、msvsmon 是代理/隔离层（目标崩溃不拖垮 IDE）并真正调 Win32 调试 API、内核提供特权接口。→ 知识点：同上「调试舞台上的参与者」一节
3. PDB 存机器码↔源码行号、变量名、类型、栈回溯数据；Release 空心断点通常是 PDB 与源码不匹配或代码被优化掉。→ 知识点：同上「调试的基石」与「常见问题与排查」两节
4. 实测：`-g` 版本比无 `-g` 大（31368 vs 16216 字节）、多出 6 个 `debug_` 段——信息住在**磁盘上的调试文件/段**里，烧录/部署时根本不进目标机的 Flash。→ 知识点：[编译器选项](../02-compiler-options.md)「输出管理与调试信息」一节（`-g` 的误区拨正）

**验证输出**：

```text
$ g++ -std=c++17 dbg.cpp -o dbg_nog && g++ -std=c++17 -g dbg.cpp -o dbg_g
$ ls -l dbg_nog dbg_g
-rwxr-xr-x 1 root root 16216 Aug 15 12:36 dbg_nog
-rwxr-xr-x 1 root root 31368 Aug 15 12:36 dbg_g
$ readelf -S dbg_nog | grep -c debug_info
0
$ readelf -S dbg_g | grep debug_info
  [29] .debug_info       PROGBITS         0000000000000000  0000306b
$ readelf -S dbg_g | grep -c "debug_"
6
```

## 7.8-B {#hw-7-8-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-8-b)

**思路**：三个实验分别在验证调试机制的三个侧面：优化如何挪动「机器码↔源码行」的映射、返回地址被踩掉后栈回溯如何崩溃、sanitizer（教材外补充）如何让溢出当场现形。

1. 断点漂移：`-O0` 下 `break compute` 落在第 5 行（函数入口），`-O2` 下落在第 6 行（`for` 行，见下方 gdb 输出）——优化器重排/合并了指令，行号映射跟着变；gdb 参数显示 `n=n@entry=10` 是因为 `-O2` 下参数当前位置（寄存器）被复用/不可读，gdb 依据 DWARF entry-value 记录从调用点状态取回入口值。这就是教材「Variable is optimized away / 行号错位」在 GNU 侧的实况。→ 知识点：[MSVC 调试原理](../msvc-debugging-internals.md)「调试的基石」一节（`/Od` 是 Debug 模式的灵魂）
2. 栈损坏：32 个 `A` 冲掉返回地址，`victim` 返回时跳进 `0x4141414141414141`（`0x41`＝`'A'`）——第二次 `bt` 全是问号帧，gdb 报 `Backtrace stopped: Cannot access memory at address 0x4141414141414149`。→ 知识点：同上「堆栈损坏」一节
3. ASan：`WRITE of size 33`、点名 `buf`（`[32, 40) 'buf'` 越界）、定位 `smash.cpp:7` 的 `strcpy`、退出码 1——同样一个 bug，三种完全不同的「报法」，这就是工具链的价值。→ 知识点：[编译器选项](../02-compiler-options.md)（`-fsanitize=address` 是编译/链接期插桩）

**验证输出**：

```text
$ gdb -q -batch -x dbg_cmds.txt ./gdb_o0      # -O0:断点在函数入口
Breakpoint 1, compute (n=10) at gdbtarget.cpp:5
5     int result = 0;
$1 = 10
$ gdb -q -batch -x dbg_cmds.txt ./gdb_o2      # -O2:断点漂到循环体
Breakpoint 1, compute (n=n@entry=10) at gdbtarget.cpp:6
6     for (int i = 1; i <= n; ++i) {
$1 = 10
$ g++ -std=c++17 -O0 -g -fno-stack-protector smash.cpp -o smash
$ ./smash AAAA
buf=AAAA
victim returned normally
$ ./smash AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA; echo "exit=$?"
Segmentation fault
exit=139
$ gdb -q -batch -x smash_cmds.txt ./smash
Breakpoint 1, victim (input=0x7fffffffe1ce 'A' <repeats 32 times>) at smash.cpp:7
#0  victim (...) at smash.cpp:7
#1  0x00005555555551c5 in main (argc=2, argv=0x7fffffffdf48) at smash.cpp:16
Program received signal SIGSEGV, Segmentation fault.
0x0000555555555195 in victim (...) at smash.cpp:9
9 }
#0  0x0000555555555195 in victim (...) at smash.cpp:9
#1  0x4141414141414141 in ?? ()
#2  0x4141414141414141 in ?? ()
#3  0x00000002f7ebc500 in ?? ()
Backtrace stopped: Cannot access memory at address 0x4141414141414149
$ g++ -std=c++17 -O0 -g -fsanitize=address smash.cpp -o smash_asan
$ ./smash_asan AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA; echo "exit=$?"
=================================================================
==381==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7155fccf0028 ...
WRITE of size 33 at 0x7155fccf0028 thread T0
    #1 0x5a2c4061c268 in victim(char const*) /tmp/cpp-v7-fix2/smash.cpp:7
    #2 0x5a2c4061c327 in main /tmp/cpp-v7-fix2/smash.cpp:16
Address 0x7155fccf0028 is located in stack of thread T0 at offset 40 in frame
    #0 0x5a2c4061c1d8 in victim(char const*) /tmp/cpp-v7-fix2/smash.cpp:5
  This frame has 1 object(s):
    [32, 40) 'buf' (line 6) <== Memory access at offset 40 overflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow /tmp/cpp-v7-fix2/smash.cpp:7 in victim(char const*)
exit=1
```

（ASan 报告含每次运行变化的地址与进程号，节选时已注明 `...` 省略。）

## 7.C-1 {#hw-7-c-1}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-7-c-1)

**思路**：这道题把三章拧成一个工程：CMake 管结构（target 语义）、编译器选项管质量（INTERFACE 警告库）、拷贝器核心管功能；CTest 让「验证」变成一条命令。

1. `project_warnings` 是 `INTERFACE` 库——它不产出二进制，只携带编译选项，谁链接它谁继承，全工程一处维护。→ 知识点：[编译器选项](../02-compiler-options.md)「CMake 中的最佳实践配置」一节（接口库复用选项）
2. `fcopy_core` 静态库 + `PUBLIC` include 目录：`fcopy` 链接它即可用头文件，无需手动 `-I`。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「平台抽象层（HAL）设计」一节
3. `option` 开关 + `enable_testing()` + `add_test`，`$<TARGET_FILE:fcopy>` 在生成期展开成真实路径传给测试脚本。→ 知识点：同上（生成器表达式）
4. 测试脚本四用例（非整块、空文件、坏源、自定义块大小），CTest 统一收口：`100% tests passed`。→ 知识点：[文件拷贝器（下）](../02-file-copier-core-implementation.md)「一个完整的测试脚本」一节

**验证输出**：

```text
$ cmake -S . -B build -G Ninja
-- Configuring done (2.0s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cpp-v7-hw45b/fcopy_proj/build
$ cmake --build build
[1/4] Building CXX object CMakeFiles/fcopy.dir/src/main.cpp.o
[2/4] Building CXX object CMakeFiles/fcopy_core.dir/src/fcopy.cpp.o
[3/4] Linking CXX static library libfcopy_core.a
[4/4] Linking CXX executable fcopy
$ ctest --test-dir build --output-on-failure
Test project /tmp/cpp-v7-hw45b/fcopy_proj/build
    Start 1: fcopy_suite
1/1 Test #1: fcopy_suite ......................   Passed    0.01 sec

100% tests passed out of 1
```

## 7.C-2 {#hw-7-c-2}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-7-c-2)

**思路**：map 文件就是宿主机的「默认链接脚本执行报告」——`MEMORY`/`SECTIONS` 在宿主机上由链接器的内置脚本承担，你要的每一条证据都能在 map 里查到地址。

1. map 里 `main` 在 `0x1127`、`.data` 段基址 `0x4000`（含 CRT 的 `data_start`）、`global_data` 在 `0x4010`（`.data` 内）、`zero_data` 在 `0x4018`（`.bss` 内）——两个变量**不同段**：非零初始化进 `.data`、零初始化进 `.bss`。→ 知识点：[链接器与链接脚本](../03-linker-and-linker-scripts.md)「不同段的作用」与「链接脚本的核心概念」两节
2. 宿主机不写链接脚本，是因为链接器内置了默认脚本（`ld --verbose` 可看全文）；嵌入式内存分散、向量表有固定地址，默认布局不成立，所以必须自己写 `MEMORY`/`SECTIONS`。→ 知识点：同上「为什么嵌入式系统需要自定义链接脚本」一节
3. gc-sections 后：段数 25→24，text 1369→1310，`never_called` 从 `T` 表消失——编译端 `-ffunction-sections` 把每个函数放进独立段，链接端 `--gc-sections` 按引用图回收死段。→ 知识点：[编译器选项](../02-compiler-options.md)「垃圾回收不用的代码」一节（编译端分区 + 链接端回收）

**验证输出**：

```text
$ grep -E " main$" plain.map | head -1
                0x0000000000001127                main
$ grep -B1 -A3 "^\.data " plain.map | head -5
.data           0x0000000000004000       0x14
 *(.data .data.* .gnu.linkonce.d.*)
 .data          0x0000000000004000        0x4 /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o
                0x0000000000004000                data_start
$ grep -E "zero_data|global_data" plain.map
                0x0000000000004010                global_data
                0x0000000000004018                zero_data
$ grep -E "never_called" plain.map | head -1
                0x0000000000001151                never_called(int)
$ objdump -h plain_elf | grep -cE "^\s+[0-9]+"
25
$ objdump -h stripped_elf | grep -cE "^\s+[0-9]+"
24
$ size plain_elf stripped_elf
   text    data     bss     dec     hex filename
   1369     564      12    1945     799 plain_elf
   1310     556      12    1878     756 stripped_elf
```

## 7.C-3 {#hw-7-c-3}

**难度 L5** · 题面见 [homework](./01-homework.md#hw-7-c-3)

**思路**：编译依赖图是 DAG（有环就是循环 include，构建系统必须报错）；「并行编译最短时间」等于 DAG 的最长路——因为关键路径上的任务只能串行，其余任务可以塞进这些间隙。Kahn 拓扑排序判环，沿拓扑序做 `finish[u] = dur[u] + max(finish[dep])` 的 DP。

1. 读入时先记名字、两遍建图（第一遍登记 id、第二遍解析依赖并统计入度），处理「依赖名出现在后面」的乱序输入。→ 知识点：[编译器选项](../02-compiler-options.md)「依赖生成：`-M`, `-MMD`」一节（编译依赖关系）、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)（构建系统必须处理依赖图）
2. Kahn 拓扑排序：入度 0 进队，出队时给后继减入度；若出队总数 < N，说明有环（互相 `#include` 的活证）。→ 知识点：同上（循环依赖不可调度）
3. DP + 回溯：沿拓扑序算最早完成时间，记录最大前驱 `best_pred`，最后从全局最晚任务回溯出关键路径。→ 知识点：同上（关键路径 = 并行编译的下界）

```cpp
// sched.cpp -- 编译依赖 DAG 的最短总时间与关键路径
// 受 UVa 452 "Project Scheduling" 启发改编:任务 -> 编译单元,依赖 -> 头文件依赖
#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <vector>

struct Job
{
    std::string name;
    int duration = 0;
    std::vector<std::string> dep_names;   // 依赖任务的名字(读入时先记名字)
    std::vector<int> deps;                // 解析后的编号
    std::vector<int> dependents;          // 谁依赖我
    int indegree = 0;
    int finish = 0;                       // 最早完成时间
    int best_pred = -1;                   // 关键路径上的前驱
};

int main()
{
    int n = 0;
    std::cin >> n;
    std::vector<Job> jobs(n);
    std::map<std::string, int> id;
    for (int i = 0; i < n; ++i) {
        std::cin >> jobs[i].name >> jobs[i].duration;
        int k = 0;
        std::cin >> k;
        for (int d = 0; d < k; ++d) {
            std::string dep;
            std::cin >> dep;
            jobs[i].dep_names.push_back(dep);
        }
        id[jobs[i].name] = i;
    }
    for (int i = 0; i < n; ++i) {
        for (const std::string& dn : jobs[i].dep_names) {
            if (id.count(dn) == 0) {
                std::cout << "error: unknown dependency '" << dn << "' of "
                          << jobs[i].name << "\n";
                return 1;
            }
            jobs[i].deps.push_back(id[dn]);
            jobs[id[dn]].dependents.push_back(i);
        }
        jobs[i].indegree = static_cast<int>(jobs[i].deps.size());
    }

    // Kahn 拓扑排序:入度为 0 的进队
    std::queue<int> q;
    for (int i = 0; i < n; ++i) {
        if (jobs[i].indegree == 0) {
            q.push(i);
        }
    }
    std::vector<int> order;
    while (!q.empty()) {
        const int u = q.front();
        q.pop();
        order.push_back(u);
        for (const int v : jobs[u].dependents) {
            if (--jobs[v].indegree == 0) {
                q.push(v);
            }
        }
    }
    if (static_cast<int>(order.size()) != n) {
        std::cout << "error: circular dependency detected (not schedulable)\n";
        return 1;
    }

    // 沿拓扑序 DP: finish[u] = dur[u] + max(finish[dep])
    int answer = 0;
    int last = -1;
    for (const int u : order) {
        int best = 0;
        jobs[u].best_pred = -1;
        for (const int d : jobs[u].deps) {
            if (jobs[d].finish > best) {
                best = jobs[d].finish;
                jobs[u].best_pred = d;
            }
        }
        jobs[u].finish = jobs[u].duration + best;
        if (jobs[u].finish > answer) {
            answer = jobs[u].finish;
            last = u;
        }
    }

    // 沿 best_pred 回溯出关键路径
    std::vector<int> path;
    for (int u = last; u != -1; u = jobs[u].best_pred) {
        path.push_back(u);
    }
    std::reverse(path.begin(), path.end());

    std::cout << "finish times:\n";
    for (int i = 0; i < n; ++i) {
        std::cout << "  " << jobs[i].name << " = " << jobs[i].finish << " s\n";
    }
    std::cout << "minimum total time = " << answer << " s\n";
    std::cout << "one critical path: ";
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i > 0) {
            std::cout << " -> ";
        }
        std::cout << jobs[path[i]].name;
    }
    std::cout << "\n";
    return 0;
}
```

**验证输出**（第二组数据：双链 + 菱形依赖；第三组：循环依赖）：

```text
$ g++ -std=c++17 -O2 -Wall -Wextra sched.cpp -o sched
$ ./sched < sample1.txt
finish times:
  core.hpp = 5 s
  core.o = 35 s
  util.o = 20 s
  app.o = 25 s
  main.o = 45 s
minimum total time = 45 s
one critical path: core.hpp -> core.o -> main.o
$ cat sample2.txt
8
config.hpp 2 0
tokenizer.o 20 1 config.hpp
parser.o 25 2 tokenizer.o config.hpp
codegen.o 30 1 config.hpp
link.o 8 2 parser.o codegen.o
main.o 12 1 link.o
doc.o 15 1 config.hpp
tests.o 10 1 parser.o
$ ./sched < sample2.txt
finish times:
  config.hpp = 2 s
  tokenizer.o = 22 s
  parser.o = 47 s
  codegen.o = 32 s
  link.o = 55 s
  main.o = 67 s
  doc.o = 17 s
  tests.o = 57 s
minimum total time = 67 s
one critical path: config.hpp -> tokenizer.o -> parser.o -> link.o -> main.o
$ cat sample3.txt
3
a.o 5 1 b.o
b.o 5 1 c.o
c.o 5 1 a.o
$ ./sched < sample3.txt; echo "exit=$?"
error: circular dependency detected (not schedulable)
exit=1
```

注意第二组里 `doc.o`（17 s）和 `tests.o`（57 s）与关键路径并行，它们不决定总时间——这就是「关键路径是下界、其余任务填间隙」的直观写照。
