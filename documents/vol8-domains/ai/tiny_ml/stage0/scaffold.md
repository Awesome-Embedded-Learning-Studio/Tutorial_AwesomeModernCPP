---
title: "工程脚手架——把工具链地基浇好"
description: "搭一个 standalone CMake23 工程,FetchContent 拉 Catch2,跑通冒烟测试。Stage 0 一行业务推理代码都不写,只确认工具链+构建+测试这条地基是通的"
chapter: 8
order: 6
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 8
prerequisites:
  - "CMake 基础"
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
  - 工具链
  - 基础
---

# 工程脚手架——把工具链地基浇好

工程工程，第一个事情是把架子搭建好，不着急立马上手干活。TinyInferCpp-Lab 的 Stage 0 只干一件事:把 standalone 的 CMake 工程搭起来,有一个能编译能跑的 Catch2 冒烟测试,`.gitignore` 忽略好构建产物。一行业务推理代码都不写,这一 stage 只确认工具链、构建系统、测试框架这条地基通,后面每次写完代码 `cmake --build` 就能拿到反馈。配套工程在 `code/volumn_codes/vol8-labs/ai/tiny_ml/stage0/`。

## 为什么不是直接写推理代码

第一周卡在"编译不过 / Catch2 拉不下来 / clangd 不工作"上,比某个算法写不出来更容易让人半途而废。Stage 0 把这些前置解决掉,还顺手强制你现在就确认工具链对 C++23 的支持,别等到 Stage 5 才发现编译器版本不够,那时候回退成本高得多。

## 工程长什么样

standalone CMake 工程,目录就这几样:

```text
stage0/
├── CMakeLists.txt          # standalone,FetchContent Catch2 v3.5.0
├── .gitignore              # build/ + .cache/
├── tests/smoke.cpp         # 工具链冒烟
└── logs/                   # 踩坑账本(实证,见后面常见坑)
```

## CMake 骨架

实际的 `CMakeLists.txt` 长这样,顺着功能顺序拆:

```cmake
cmake_minimum_required(VERSION 4.1)
project(tamcpp_mlinfra LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

头三行锁标准。`CMAKE_CXX_STANDARD 23` 配 `STANDARD_REQUIRED ON`,把 C++23 钉死,不让编译器偷偷降级。光写 `STANDARD` 不加 `STANDARD_REQUIRED`,某些编译器会静悄悄降到它能支持的最高标准,你后面用到 C++23 特性时炸出一个莫名其妙的报错,排都排不到标准头上。`CMAKE_EXPORT_COMPILE_COMMANDS ON` 是给 clangd 生成 `compile_commands.json`,IDE 的跳转、补全、诊断全指望它,不开这句,写代码会很难受。

```cmake
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)

FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
```

这段把 Catch2 在配置期从 Git 拉下来,直接 `add_subdirectory` 同构编译进工程,不靠预装或 submodule,版本被 `GIT_TAG v3.5.0` 锁死。

三步各司其职。`FetchContent_Declare` 只登记 name、来源、tag,不触发下载。`FetchContent_MakeAvailable` 才在首次配置时 clone、`add_subdirectory`、定义出 `Catch2::Catch2WithMain` 这个 target,顺带设一个 `<depname>_SOURCE_DIR` 变量供你引用,变量名规则是 depname 全小写,所以这里是 `catch2_SOURCE_DIR`。最后那行 `list(APPEND CMAKE_MODULE_PATH .../extras)` 把 Catch2 自带的辅助 `.cmake`(`Catch.cmake` 等,里面含 `catch_discover_tests`)挂进 CMake 的模块搜索路径,后面想 `include(Catch)` 才找得到。

下载的源码落在 `build/_deps/catch2-src/`,中间产物在 `-build/`,二次配置不重拉,源码树干干净净。这套机制的实证,包括拉不下来时怎么救急,记在 `logs/002-fetchcontent-catch2.md`。

```cmake
add_executable(smoke_catch2 tests/smoke.cpp)
target_link_libraries(smoke_catch2 PRIVATE Catch2::Catch2WithMain)

target_compile_options(smoke_catch2 PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
)
```

`target_compile_options` 是现代 CMake 的 target-based 写法,选项挂在指定 target 上,不污染所有目标,不像老式那种全局 `add_compile_options()`。`PRIVATE` 意思是只编译自己、不往下传。警告标志几乎一律 PRIVATE,下游才不管你怎么编译它,而且警告标志编译器强相关,一旦 PUBLIC 传下去,换个编译器立刻炸。

最后两行生成器表达式要拆开讲。结论先放这:`-Wall -Wextra -Wpedantic -g` 全是 GCC/Clang 的私有方言,MSVC 一个都不认。MSVC 有自己的一套,警告级用 `/W4`(实用最高档,`/Wall` 会刷屏没法用),严格标准符合用 `/permissive-`(禁掉非标准扩展),调试信息用 `/Zi`(写进 PDB)。所以工程里按编译器 ID 分挂,MSVC 走一条,非 MSVC 走另一条。

第二条特意写成 `$<NOT:$<CXX_COMPILER_ID:MSVC>>` 而不是枚举 `$<CXX_COMPILER_ID:GNU,Clang>`。前者意思是只要不是 MSVC 就走 GCC/Clang 那套,自动覆盖 Intel、LLVM 这些同样认 `-Wall` 的编译器,不用每加一个编译器去改清单。完整的标志对照在 `logs/003-target-compile-options.md`。

## 为什么是 C++23 而不是 C++20

Stage 0 本身不依赖任何 C++23 特性,你拿 C++20 也能把这 stage 跑通。但后面 Stage 1 起要用 `consteval`、更完整的 `constexpr`、`std::expected`,标准现在就设成 23,省得以后回头改。

## 冒烟测试:挑个狠一点的探针

```cpp
#include <catch2/catch_test_macros.hpp>
#include <print>

TEST_CASE("Smoke up the labs") {
    std::print("Our smoke Test");
}
```

冒烟测试的用处就是证明链路通了,挑个狠一点的探针更划算。这里特意用 `std::print`,它要 C++23 的 `<print>` 头可达,一句话同时压上工具链、Catch2、CMake、C++23 标准库四件事,比老老实实写一句 `REQUIRE(1 + 1 == 2)` 探得深。

## 验证

```bash
cmake -S . -B build           # 首次 FetchContent 拉 Catch2,需联网
cmake --build build -j
./build/smoke_catch2
```

第 3 步会打印 `Our smoke Test` 并报 `All tests passed`。再人工确认一下,IDE 里 clangd 能跳进 `smoke.cpp`、能补全 Catch2 的宏,说明 `compile_commands.json` 真生效了。

## 常见坑

::: warning FetchContent 拉不下来 Catch2
WSL 访问 github 不稳,失败日志一般是 `Failed to connect to github.com port 443`。救急办法是手动浅克隆一份,用预放置目录替换自动下载:

```bash
git clone --depth 1 -b v3.5.0 https://github.com/catchorg/Catch2.git /tmp/catch2
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/catch2
```

变量名规则是 `FETCHCONTENT_SOURCE_DIR_<大写DEPNAME>`,本质是替换 `GIT_REPOSITORY` 的下载源。实证见 `logs/002`。
:::

::: warning 编译器不支持 C++23
`set(CMAKE_CXX_STANDARD 23)` 要 GCC 13+ / Clang 16+ / MSVC VS2022 17.6+。开工前先自测一下:

```bash
g++ --version
echo 'int main(){return 0;}' | g++ -std=c++23 -x c++ - -o /tmp/cxx23_smoke && echo "C++23 OK"
```

本机实测 g++ 16.1.1、clang++ 22.1.6(见 `logs/003`)。报错就升级编译器,别降标准,降了后面 Stage 1 起会顶不住。
:::

::: warning clangd 报"找不到头文件"
九成是 `compile_commands.json` 没生成,或者 clangd 没指向 `build/`。确认 `CMAKE_EXPORT_COMPILE_COMMANDS ON` 已经配上。`.cache/clangd` 是 clangd 建索引用的,别误删也别提交,已经 `.gitignore` 了。
:::

::: warning 在仓库根目录跑 cmake
别在仓库根跑 cmake。这个工程就窝在自己目录 `stage0/` 里构建,`build/` 产物留在本目录,而且已经被 `.gitignore` 忽略。
:::
