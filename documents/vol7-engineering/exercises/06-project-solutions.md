---
title: "卷 7 Project 参考实现"
description: "fcopy++ 工程化文件拷贝器的完整参考实现：分层任务（核心/进阶/质量门/终极）逐段讲解、每步标注知识点链接，含完整 CMake 工程、进度与 ETA、警告/sanitizer/CTest 质量门、C++20 模块变体、八组合矩阵与增量构建证据，全部输出在 WSL Arch（g++ 16 / clang++ 22 / CMake 4.4.2）的 /tmp 真实运行得到。"
chapter: 7
order: 6
tags: [host, intermediate, cpp-modern, CMake, 工程实践]
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 24
prerequisites:
  - "卷 7 Project 题面"
related:
  - "卷 7 全部章节（第 7.1~7.8 章）"
---

# 卷 7 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1、clang++ 22.1.8、CMake 4.4.2、Ninja 1.13.2）下真实运行得到，全部构建在 `/tmp` 独立目录。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。进度行里的 `\r` 在终端上是「覆盖本行」，日志里就是字面回车，别当成乱码。

## 核心任务（L2）：能跑起来的拷贝器 {#pj-core}

**思路**：头文件立契约（`CopyStats` 是「带测量结果」的拷贝收据），实现走教材的分块读写主线；CMake 里静态库 + 可执行分离，为后面所有层铺路。CTest 到 L4「pj-gates」才引入（那里才上 `enable_testing()`/`add_test`），本层验收以手动运行为准：`cmp` 逐字节一致 + 返回码 0。

**`include/fcopypp/copier.hpp`**——契约：统计结构 + 单一入口。→ 知识点：[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)「接口设计」一节

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace fcpp
{
struct CopyStats
{
    std::uintmax_t total = 0;
    std::uintmax_t copied = 0;
    double elapsed_seconds = 0.0;
};

bool copy_file(const std::string& src, const std::string& dst,
               std::size_t chunk_size, CopyStats* stats_out = nullptr);
}
```

**`src/copier.cpp`**——核心实现：检查 → 双流 → 缓冲 → 主循环 → 校验 → 统计。注意 `fs::is_directory` 这道额外防线：`exists` 对目录返回 `true`，不加它就得靠 `file_size` 的异常兜（那是 Homework 7.4-B 的坑）。→ 知识点：[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)「核心读写循环」「收尾工作」两节、[文件拷贝器（上）](../01-file-copier-requirements-and-framework.md)「前置检查」一节

```cpp
#include "fcopypp/copier.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace fcpp
{
bool copy_file(const std::string& src, const std::string& dst,
               std::size_t chunk_size, CopyStats* stats_out)
{
    const auto t0 = std::chrono::steady_clock::now();
    try {
        if (!fs::exists(src)) {
            std::cerr << "Source file does not exist: " << src << "\n";
            return false;
        }
        if (fs::is_directory(src)) {
            std::cerr << "Source is a directory: " << src << "\n";
            return false;
        }
        const std::uintmax_t total = fs::file_size(src);

        std::ifstream in(src, std::ios::binary);
        if (!in) {
            std::cerr << "Failed to open source file for reading: " << src << "\n";
            return false;
        }
        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "Failed to open destination file for writing: " << dst << "\n";
            return false;
        }

        std::vector<char> buffer(chunk_size);
        std::uintmax_t copied = 0;
        while (in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = in.gcount();
            if (got <= 0) {
                break;
            }
            out.write(buffer.data(), got);
            if (!out) {
                std::cerr << "Write error while writing to: " << dst << "\n";
                return false;
            }
            copied += static_cast<std::uintmax_t>(got);
        }
        out.flush();
        out.close();
        in.close();

        if (copied != total) {
            std::cerr << "Size mismatch after copy. src=" << total
                      << " dst=" << copied << "\n";
            return false;
        }

        const auto t1 = std::chrono::steady_clock::now();
        if (stats_out != nullptr) {
            stats_out->total = total;
            stats_out->copied = copied;
            stats_out->elapsed_seconds =
                std::chrono::duration<double>(t1 - t0).count();
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << "\n";
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return false;
    }
}
}
```

**`CMakeLists.txt`（核心版）**——库与可执行分离。→ 知识点：[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「平台抽象层（HAL）设计」一节（target 语义）

```cmake
cmake_minimum_required(VERSION 3.16)
project(fcopypp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(fcopypp_core STATIC src/copier.cpp)
target_include_directories(fcopypp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(fcopy src/main.cpp)
target_link_libraries(fcopy PRIVATE fcopypp_core)
```

**`src/main.cpp`（核心版）**——解析两个参数、调用 `copy_file`、按返回码退出；本层不打印任何统计行（`Copied ... MB/s` 是 L3 进度层加上的）。→ 知识点：[文件拷贝器（上）：需求分析与基础框架](../01-file-copier-requirements-and-framework.md)「接口设计」一节（返回码是 CLI 程序的成败契约）

```cpp
#include <iostream>

#include "fcopypp/copier.hpp"

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <source> <destination>\n";
        return 1;
    }
    return fcpp::copy_file(argv[1], argv[2], 8 * 1024) ? 0 : 1;
}
```

**验证输出**（热身 + 配置 + 构建 + 拷贝校验）：

```text
$ g++ -std=c++17 -Wall -Wextra -c src/copier_skel.cpp -Iinclude -o copier_skel.o; echo "exit=$?"
exit=0
$ cmake -S . -B build -G Ninja
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (2.5s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cpp-fix7-core/fcopypp/build
$ cmake --build build
[1/4] Building CXX object CMakeFiles/fcopy.dir/src/main.cpp.o
[2/4] Building CXX object CMakeFiles/fcopypp_core.dir/src/copier.cpp.o
[3/4] Linking CXX static library libfcopypp_core.a
[4/4] Linking CXX executable fcopy
$ head -c 524288 /dev/urandom > demo.bin
$ ./build/fcopy demo.bin demo_copy.bin; echo "exit=$?"
exit=0
$ cmp demo.bin demo_copy.bin && echo "content identical"
content identical
```

核心版的 `main` 是静默设计：成功路径一行都不打印，靠返回码报告成败（`echo $?` 拿到 0），逐字节一致性交给 `cmp` 对账；`Copied ... MB/s` 那样的统计行要到 L3 进度层才会出现。

## 进阶任务（L3）：进度反馈与 CLI {#pj-extra}

**思路**：进度三函数是纯数学（字节数、秒、百分比），从拷贝逻辑里拆出来，既好测、又好模块化——这一步的拆分为终极层的模块化埋了伏笔。

**`include/fcopypp/progress.hpp` + `src/progress.cpp`**——三个纯函数，各自守住一个边界：`total == 0`（空文件直接 100%）、`elapsed < 1e-9`（防除零）、`speed < 1e-6 || copied >= total`（还没速度或已完成则 ETA 为 0）。→ 知识点：[文件拷贝器（下）：核心实现与实战测试](../02-file-copier-core-implementation.md)「百分比和大小显示」「ETA 计算」两节

```cpp
#include "fcopypp/progress.hpp"

namespace fcpp
{
int percent_of(std::uintmax_t copied, std::uintmax_t total)
{
    if (total == 0) {
        return 100;
    }
    const double fraction = static_cast<double>(copied) / static_cast<double>(total);
    return static_cast<int>(fraction * 100.0);
}

double speed_bps(std::uintmax_t copied, double elapsed_seconds)
{
    if (elapsed_seconds < 1e-9) {
        return 0.0;
    }
    return static_cast<double>(copied) / elapsed_seconds;
}

double eta_seconds(std::uintmax_t copied, std::uintmax_t total, double speed)
{
    if (speed < 1e-6 || copied >= total) {
        return 0.0;
    }
    return static_cast<double>(total - copied) / speed;
}
}
```

**`src/main.cpp`（进阶版）**——`\r` 动态进度 + 可变参数解析 + 最终统计。→ 知识点：[文件拷贝器（下）](../02-file-copier-core-implementation.md)「回车符的妙用」一节、[编译器选项](../02-compiler-options.md)（`-D` 的现代替代是命令行参数而非宏）

```cpp
#include <cstddef>
#include <iostream>
#include <string>

#include "fcopypp/copier.hpp"
#include "fcopypp/progress.hpp"

namespace
{
void render_progress(const fcpp::CopyStats& s)
{
    const int pct = fcpp::percent_of(s.copied, s.total);
    const double spd = fcpp::speed_bps(s.copied, s.elapsed_seconds);
    const double eta = fcpp::eta_seconds(s.copied, s.total, spd);
    std::cout << '\r' << pct << "% | " << spd / (1024.0 * 1024.0)
              << " MB/s | ETA " << eta << "s   " << std::flush;
}
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <source> <destination> [--chunk-size N]\n";
        return 1;
    }
    std::size_t chunk = 8 * 1024;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--chunk-size" && i + 1 < argc) {
            chunk = static_cast<std::size_t>(std::stoull(argv[i + 1]));
            ++i;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    fcpp::CopyStats stats;
    const bool ok = fcpp::copy_file(argv[1], argv[2], chunk, &stats);
    if (ok) {
        render_progress(stats);
        std::cout << "\n";
        const double spd = fcpp::speed_bps(stats.copied, stats.elapsed_seconds);
        std::cout << "Copied " << stats.copied << " bytes in "
                  << stats.elapsed_seconds << " s ("
                  << spd / (1024.0 * 1024.0) << " MB/s)\n";
        return 0;
    }
    std::cerr << "Copy failed!\n";
    return 1;
}
```

**验证输出**（最终进度行 + 总耗时 + 三条错误路径）：

```text
$ head -c 524288 /dev/urandom > demo.bin
$ ./build/fcopy demo.bin demo_copy.bin
100% | 2858.3 MB/s | ETA 0s
Copied 524288 bytes in 0.000174929 s (2858.3 MB/s)
$ ./build/fcopy nope.bin out.bin; echo "exit=$?"
Source file does not exist: nope.bin
Copy failed!
exit=1
$ ./build/fcopy demo.bin /no_dir/out.bin; echo "exit=$?"
Failed to open destination file for writing: /no_dir/out.bin
Copy failed!
exit=1
$ ./build/fcopy . out.bin; echo "exit=$?"
Source is a directory: .
Copy failed!
exit=1
$ dd if=/dev/zero of=big64.bin bs=1M count=64 2>/dev/null
$ ./build/fcopy big64.bin big64_copy.bin | tail -c 120
100% | 3025.62 MB/s | ETA 0s
Copied 67108864 bytes in 0.0211527 s (3025.62 MB/s)
```

## 再进阶任务（L4）：把门装上 {#pj-gates}

**思路**：三道门各守一段：`-Wconversion`（教材外补充：把隐式窄化/符号转换升级成警告）逼你显式化每个窄化/符号转换（本实现里所有 `static_cast` 就是它的产物）；CTest 把测试脚本变成一条命令；sanitizer（教材外补充：编译/链接期插桩）在运行时抓内存与 UB。

**`CMakeLists.txt`（全量版）**——`INTERFACE` 警告库 + `option` 开关 + 生成器表达式。→ 知识点：[编译器选项](../02-compiler-options.md)「CMake 中的最佳实践配置」一节（INTERFACE 库 + `$<$<CONFIG:...>>`）、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)「使用生成器表达式」一节

```cmake
cmake_minimum_required(VERSION 3.16)
project(fcopypp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FCOPY_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(FCOPY_BUILD_TESTS "Build and register tests" ON)

add_library(project_warnings INTERFACE)
target_compile_options(project_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    $<$<BOOL:${FCOPY_WARNINGS_AS_ERRORS}>:-Werror>
)

add_library(fcopypp_core STATIC src/copier.cpp src/progress.cpp)
target_include_directories(fcopypp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(fcopypp_core PUBLIC project_warnings)

add_executable(fcopy src/main.cpp)
target_link_libraries(fcopy PRIVATE fcopypp_core)
target_compile_options(fcopy PRIVATE
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-O2>
)

if(FCOPY_BUILD_TESTS)
    enable_testing()
    add_test(NAME fcopy_suite
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh $<TARGET_FILE:fcopy>)
endif()
```

**`tests/run_tests.sh`**——四用例，`cmp -s` 逐字节对账。→ 知识点：[文件拷贝器（下）](../02-file-copier-core-implementation.md)「一个完整的测试脚本」一节

```bash
#!/bin/bash
# run_tests.sh -- fcopypp 验收测试(被 CTest 调用)
set -u
BIN="$1"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

fail=0

# 1: 非整块大小,内容逐字节一致
head -c 100003 /dev/urandom > odd.bin
"$BIN" odd.bin odd_copy.bin > /dev/null
if cmp -s odd.bin odd_copy.bin; then
    echo "PASS test1: odd-size copy byte-identical"
else
    echo "FAIL test1: odd-size copy differs"
    fail=1
fi

# 2: 空文件
: > empty.bin
"$BIN" empty.bin empty_copy.bin > /dev/null
if [ -f empty_copy.bin ] && [ ! -s empty_copy.bin ]; then
    echo "PASS test2: empty file copy"
else
    echo "FAIL test2: empty file copy"
    fail=1
fi

# 3: 源不存在必须失败
if "$BIN" no_such.bin out.bin 2>/dev/null; then
    echo "FAIL test3: nonexistent source should fail"
    fail=1
else
    echo "PASS test3: nonexistent source rejected"
fi

# 4: 自定义块大小仍逐字节一致
head -c 30007 /dev/urandom > small.bin
"$BIN" small.bin small_copy.bin --chunk-size 1024 > /dev/null
if cmp -s small.bin small_copy.bin; then
    echo "PASS test4: custom chunk size copy byte-identical"
else
    echo "FAIL test4: custom chunk size copy differs"
    fail=1
fi

exit $fail
```

**验证输出**（警告门构建 + 普通 ctest + sanitizer ctest）：

```text
$ cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFCOPY_WARNINGS_AS_ERRORS=ON
-- Configuring done (1.9s)
-- Generating done (0.0s)
$ cmake --build build
[1/5] Building CXX object CMakeFiles/fcopypp_core.dir/src/progress.cpp.o
[2/5] Building CXX object CMakeFiles/fcopy.dir/src/main.cpp.o
[3/5] Building CXX object CMakeFiles/fcopypp_core.dir/src/copier.cpp.o
[4/5] Linking CXX static library libfcopypp_core.a
[5/5] Linking CXX executable fcopy
$ ctest --test-dir build --output-on-failure
Test project /tmp/cpp-v7-pj/fcopypp/build
    Start 1: fcopy_suite
1/1 Test #1: fcopy_suite ......................   Passed    0.01 sec

100% tests passed out of 1
$ cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DFCOPY_WARNINGS_AS_ERRORS=ON \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
$ cmake --build build-asan
[4/5] Linking CXX static library libfcopypp_core.a
[5/5] Linking CXX executable fcopy
$ ctest --test-dir build-asan --output-on-failure
Test project /tmp/cpp-v7-pj/fcopypp/build-asan
    Start 1: fcopy_suite
1/1 Test #1: fcopy_suite ......................   Passed    0.03 sec

100% tests passed out of 1
$ grep -cE "ERROR: AddressSanitizer|runtime error" <(ctest --test-dir build-asan 2>&1) || echo "0 sanitizer reports"
0 sanitizer reports
```

## 终极挑战（L5）：模块化 + 矩阵发布 + 增量证据 {#pj-l5}

**思路**：三件事共用同一条主线——把「可发布」三个字落实到证据。模块化这一步有个真坑：模块接口直接引 `std::string` 出接口，本机 GCC 会在消费方撞出标准库实体的冲突声明（Lab 步骤 6 有完整报错），所以接口只留核心语言类型（`unsigned long long` + `double`）；矩阵发布证明工程在两个编译器、两种配置、两种生成器下全绿；增量证据证明依赖跟踪精确到文件。

**`copy_stats.cppm`**——零标准库依赖的模块接口。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)「为什么要用模块」一节（接口与实现解耦）

```cpp
export module copy_stats;

// 进度数学做成纯核心语言类型的模块接口(为什么,见 Lab 步骤 6 的坑)
export int percent_of(unsigned long long copied, unsigned long long total)
{
    if (total == 0) {
        return 100;
    }
    const double fraction =
        static_cast<double>(copied) / static_cast<double>(total);
    return static_cast<int>(fraction * 100.0);
}

export double speed_bps(unsigned long long copied, double elapsed_seconds)
{
    if (elapsed_seconds < 1e-9) {
        return 0.0;
    }
    return static_cast<double>(copied) / elapsed_seconds;
}

export double eta_seconds(unsigned long long copied, unsigned long long total,
                           double speed)
{
    if (speed < 1e-6 || copied >= total) {
        return 0.0;
    }
    return static_cast<double>(total - copied) / speed;
}
```

**`CMakeLists.txt`（模块变体）**——`FILE_SET CXX_MODULES` + `-fmodules`（教材外补充：GCC 侧开启 C++20 模块的开关，教材的 MSVC 侧由 IDE 自动扫描）；`copier.cpp` 原样复用（接口无 std 依赖的另一个好处：模块化不用动它）。→ 知识点：[VS2026 使用 C++ 模块](../cpp-modules-on-vs2026.md)（CMake 承担了 MSVC 里 IDE 的扫描）、[交叉编译与 CMake](../01-cross-compilation-and-cmake.md)

```cmake
cmake_minimum_required(VERSION 3.28)
project(fcopypp_mod LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(fcopy_mod src/main_mod.cpp src/copier.cpp)
target_sources(fcopy_mod PRIVATE
    FILE_SET CXX_MODULES FILES copy_stats.cppm
)
target_include_directories(fcopy_mod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(fcopy_mod PRIVATE -fmodules -Wall -Wextra)
```

**验证输出**（模块构建 + 运行 + 矩阵 + 增量）：

```text
$ cmake -S fcopypp_mod -B modbuild -G Ninja && cmake --build modbuild
[1/8] Scanning /tmp/cpp-v7-pjfin/fcopypp_mod/copy_stats.cppm for CXX dependencies
[2/8] Scanning /tmp/cpp-v7-pjfin/fcopypp_mod/src/main_mod.cpp for CXX dependencies
[3/8] Scanning /tmp/cpp-v7-pjfin/fcopypp_mod/src/copier.cpp for CXX dependencies
[4/8] Generating CXX dyndep file CMakeFiles/fcopy_mod.dir/CXX.dd
[5/8] Building CXX object CMakeFiles/fcopy_mod.dir/copy_stats.cppm.o
[6/8] Building CXX object CMakeFiles/fcopy_mod.dir/src/main_mod.cpp.o
[7/8] Building CXX object CMakeFiles/fcopy_mod.dir/src/copier.cpp.o
[8/8] Linking CXX executable fcopy_mod
$ ./modbuild/fcopy_mod fcopypp_mod/demo.bin fcopypp_mod/demo_copy.bin
100% | 2348.51 MB/s | ETA 0s
Copied 524288 bytes in 0.000212901 s
$ cmp fcopypp_mod/demo.bin fcopypp_mod/demo_copy.bin && echo "content identical"
content identical
$ for CC in g++ clang++; do ... done      # 矩阵脚本:每格 -DFCOPY_WARNINGS_AS_ERRORS=ON
cc      config  generator          text  result
g++     Debug   Ninja             25311  build OK (zero warnings)
g++     Debug   Unix Makefiles    25311  build OK (zero warnings)
g++     Release Ninja             15263  build OK (zero warnings)
g++     Release Unix Makefiles    15263  build OK (zero warnings)
clang++ Debug   Ninja             27024  build OK (zero warnings)
clang++ Debug   Unix Makefiles    27024  build OK (zero warnings)
clang++ Release Ninja             11734  build OK (zero warnings)
clang++ Release Unix Makefiles    11734  build OK (zero warnings)
$ touch src/main.cpp && ninja -C build -v | grep -oE "\-c [^ ]*\.cpp"
-c /tmp/cpp-v7-pjfin/fcopypp/src/main.cpp
$ touch src/copier.cpp && ninja -C build -v | grep -oE "\-c [^ ]*\.cpp"
-c /tmp/cpp-v7-pjfin/fcopypp/src/copier.cpp
$ ninja -C build
ninja: no work to do.
```

矩阵表里 Debug 的 text 比 Release 大，是 Debug 的 `-Og -g3` 与 Release 的 `-O2` 生成机器码的差异——`-g3` 的调试信息住在 `.debug_*` 段里、不进 `text` 统计，大小关系取决于代码与编译器、本机没有普适机制；两个编译器四格全绿说明这份代码在 `-Wconversion` 级别下两边都干净——这就是「可发布」的证据。增量实验每次只重编被触碰的那个文件，说明构建系统的时间戳 + 依赖跟踪精确到了单文件——它背后就是教材讲的 `-MMD` 依赖生成。
