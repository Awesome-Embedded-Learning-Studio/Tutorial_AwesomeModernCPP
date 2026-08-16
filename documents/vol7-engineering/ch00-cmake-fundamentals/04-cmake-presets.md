---
title: "CMakePresets.json——从 cmake -D 老式到 --preset 可复现"
description: "讲透 CMakePresets.json 怎么把 -D 老式姿势固化进版本控制：configurePresets/buildPresets/testPresets 三类、hidden+inherits 组合、CMakeUserPresets.json 个人覆盖"
chapter: 7
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 16
prerequisites:
  - "vol7 ch00 01: CMake 是什么——构建系统生成器的两段式流水线"
  - "vol7 ch00 02: Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求"
related:
  - "交叉编译与 CMake"
  - "编译器选项"
---

# CMakePresets.json——从 cmake -D 老式到 --preset 可复现

前面两篇咱们 configure 时敲的命令都长一个样：`cmake -B build -G Ninja`。真实工程里这一行通常远远不止这么短。带 build type、带 toolchain 文件、带几个缓存变量之后，命令会膨胀成下面这种样子：

```text
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

这种命令一长就开始出问题。笔者自己就踩过：把 `CMAKE_BUILD_TYPE` 抄成 `CMAKE_BUILD-TYPE`，configure 不报错，默默走空配置，编出来的二进制带一堆调试符号；同事之间互相问「你那个 vcpkg 路径填啥」；CI 里把这条命令嵌进 YAML，改一个选项就 PR 一片红。CMake 3.19 引入了 `CMakePresets.json`，把这些散在命令行里的 `-D`、Generator 选择、构建目录统一固化进一个 JSON 文件，最后命令瘦成 `cmake --preset debug` 一行。这篇就讲怎么用它，以及它和 vcpkg toolchain、VSCode CMake Tools 怎么挂上钩。

## 为什么需要 Presets：-D 老式的四个痛点

在动手写 JSON 之前，先把「为什么这件事值得做」说透。咱们对照前面几篇用过的命令，逐一拆 -D 老式姿势的毛病。

第一是命令长、容易抄错。上面那条命令 130 多个字符，跨多行。`CMAKE_BUILD_TYPE`、`CMAKE_TOOLCHAIN_FILE` 这些 key 名一个字母不对，CMake 都不会报错——它把不认识的变量静默写进缓存，然后您拿到的就是一份「看起来配过、实际啥也没设」的构建树，问题往往要等到运行时才暴露。笔者在 `CMAKE_BUILD-TYPE`（下划线打成了连字符）这个笔误上花过半天排查。

第二是不可复现。命令只活在您的终端 history 里。换台机器、换个终端窗口、或者半个月后回来继续这工程，命令早就没了，只能凭记忆重敲一遍。哪怕记得大概，参数顺序、某个 `-D` 是不是开了，谁也不敢打包票。

第三是团队各敲各的。同一个工程，A 用 `Release`、B 用 `RelWithDebInfo`、C 忘了指定 `CMAKE_BUILD_TYPE`，三台机器编出来三份行为不同的二进制。bug 在 B 那里复现，到 A 那里就消失，回头一查是 build type 不一致——这种扯皮在没规范的项目里几乎是常态。

第四是 CI 难固化。CI 脚本要把命令完整复制进 YAML，每个 `-D` 都是潜在的拼写陷阱。改一个编译选项要在两个地方（本地命令 + CI YAML）同步改，时间一长必然漂移。

Presets 这套机制就是冲着这四个痛点来的。把「用哪些 `-D`、用哪个 Generator、构建到哪个目录」写进 `CMakePresets.json`，这个文件进版本控制，团队和 CI 共享同一份配置。本地敲 `cmake --preset debug`，CI 里也是 `cmake --preset debug`，命令两边一字不差，构建行为可复现。

## CMakePresets.json 结构

`CMakePresets.json` 的顶层有三大类预设，对应 CMake 工作流的三段：

`configurePresets` 对应 `cmake --preset`，固化 configure 阶段的 `-D`、Generator、`binaryDir`。这是最常用的一类。

`buildPresets` 对应 `cmake --build --preset`，固化 build 阶段的 `--target`、`--config`、并行度等参数。schema version 2（CMake 3.20）才加入。

`testPresets` 对应 `ctest --preset`，固化测试阶段的 filter、输出格式等。同样是 schema version 2 引入。

咱们先看一个完整的最小可用示例，再拆字段。下面这份 `CMakePresets.json` 是笔者为本文实测用的，一个 hidden 的 `base` preset 设通用项，两个继承它的 `debug` 和 `release` 分别设 `CMAKE_BUILD_TYPE`：

```json
{
    "version": 3,
    "cmakeMinimumRequired": {
        "major": 3,
        "minor": 21,
        "patch": 0
    },
    "configurePresets": [
        {
            "name": "base",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "cacheVariables": {
                "CMAKE_CXX_STANDARD": "17",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_CXX_EXTENSIONS": "OFF"
            }
        },
        {
            "name": "debug",
            "displayName": "Debug (含 -g -O0)",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug"
            }
        },
        {
            "name": "release",
            "displayName": "Release (含 -O3 -DNDEBUG)",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "debug",
            "configurePreset": "debug"
        },
        {
            "name": "release",
            "configurePreset": "release"
        }
    ]
}
```

逐字段拆。顶层 `version` 是 **JSON schema 的版本号**，不是 CMake 的版本号。当前最高到 9（CMake 3.27 引入），3 是个稳妥的下限，覆盖了 `configurePresets` + `buildPresets` + `testPresets` 全部基础能力，CMake 3.21 起原生支持。schema 版本和 `cmakeMinimumRequired` 是两件事：前者声明「这份 JSON 按哪个版本的 schema 写」，后者声明「跑这份 JSON 至少要 CMake 多新」。低于这个版本的 CMake 看到 `CMakePresets.json` 直接拒绝，避免老 CMake 解析不了新字段却默默继续。

`configurePresets` 是个数组，每个元素是一个 preset。`base` 这个 preset 有几个关键字段。

`name` 是 preset 的唯一标识，`cmake --preset` 后面跟的就是它。

`hidden: true` 表示这个 preset 不能被 `--preset` 直接使用，也不会出现在 `--list-presets` 输出里，它只作为基类被别的 preset 继承。咱们马上用 `cmake --preset base` 实测，CMake 会直接报错挡住。

`generator` 和 `binaryDir` 分别固化了 `-G` 和 `-B`。注意 `binaryDir` 写成 `${sourceDir}/build/${presetName}`，这里有两层宏展开：`${sourceDir}` 是工程根目录的绝对路径，`${presetName}` 是当前 preset 的名字（比如 `debug`、`release`）。这样写的好处是不同 preset 各自落到独立的构建目录，`build/debug` 和 `build/release` 互不干扰，切换 build type 不用 `rm -rf build` 重来。

`cacheVariables` 是 `-D` 的固化。每一项 `key: value` 就等价于 `-Dkey=value`。值可以是字符串、布尔、`null`（表示 `UNINITIALIZED` 类型），也可以是带 `type` 字段的对象（精确控制缓存变量类型）。

接下来看 `debug` 和 `release` 怎么继承 `base`。`inherits: "base"` 表示「这个 preset 把 `base` 的所有字段继承过来，自己再覆盖一部分」。这里只覆盖了 `cacheVariables.CMAKE_BUILD_TYPE`：`debug` 设成 `Debug`，`release` 设成 `Release`。`generator`、`binaryDir`、`CMAKE_CXX_STANDARD` 这些 `base` 上的通用字段原封不动继承下来。

`inherits` 接受单个字符串或字符串数组。数组场景下，多个父 preset 提供同名字段时，**数组里靠前的优先**——这点和 C++ 多继承的歧义处理不一样，CMake 是有确定性顺序的。

`buildPresets` 部分简单：每个 build preset 通过 `configurePreset` 字段绑定到一个 configure preset。`cmake --build --preset debug` 就知道去 `build/debug` 这个 `binaryDir` 下执行构建，不用再写 `cmake --build build/debug`。

::: details schema version 选几合适
官方文档里 schema 版本一路从 1 涨到 9。选哪个取决于您要用的新特性。version 1（CMake 3.19）只有 `configurePresets`，没有 build/test presets；version 2（3.20）补齐 `buildPresets`/`testPresets`；version 3（3.21）加入 `cmakeMinimumRequired` 字段和更宽松的宏展开。再往后主要是给 CI 集成、条件化 include 等高级场景打补丁。笔者的默认选择是 3，能覆盖绝大多数工程需求，同时保证 CMake 3.21+ 就能解析。
:::

## 用起来：从 configure 到 build 的真实输出

光看 JSON 不过瘾，咱们跑一遍。这份 `CMakePresets.json` 配套的最小工程（`CMakeLists.txt` + `main.cpp`）在仓库 `code/examples/vol7/cmake-fundamentals/04-presets/` 下。先看 CMake 能识别出哪些 preset：

```text
$ cmake --list-presets
Available configure presets:

  "debug"   - Debug (含 -g -O0)
  "release" - Release (含 -O3 -DNDEBUG)
```

`--list-presets` 把所有非 hidden 的 configure preset 列出来，连同 `displayName` 一起。注意 `base` 没出现——它被 `hidden` 挡住了。如果硬要 `cmake --preset base`，CMake 直接报错：

```text
$ cmake --preset base
CMake Error: Cannot use hidden configure preset in /tmp/cmake-presets-demo: "base"
```

这正是 hidden preset 的语义：只做基类，不直接用。这个设计避免了团队成员误用「只配了一半」的 preset。

跑 `debug` preset：

```text
$ cmake --preset debug
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-presets-demo/build/debug
```

最后一行是关键证据：构建文件落到了 `build/debug`。`${sourceDir}/build/${presetName}` 这层宏展开起作用了。再跑 `release`，构建目录是 `build/release`，两个互不干扰：

```text
$ ls build/
debug  release
```

接着用 build preset 跑构建：

```text
$ cmake --build --preset debug
[1/2] Building CXX object CMakeFiles/app.dir/main.cpp.o
[2/2] Linking CXX executable app
```

`cmake --build --preset debug` 等价于 `cmake --build build/debug`，但您不用记 `binaryDir` 长啥样，preset 帮您记着。

光跑通不够，咱们验证一下 `cacheVariables` 里的 `CMAKE_BUILD_TYPE` 真的流到了编译命令里。在 `main.cpp` 里笔者埋了一行 `#ifdef NDEBUG` 区分两种 build。先看 `release` 的二进制实际拿到的 flags，去 `build.ninja` 里扒：

```text
$ grep FLAGS build/release/build.ninja | head -2
  FLAGS = -O3 -DNDEBUG -std=c++17
  FLAGS = -O3 -DNDEBUG

$ grep FLAGS build/debug/build.ninja | head -2
  FLAGS = -g -std=c++17
  FLAGS = -g
```

`release` 拿到 `-O3 -DNDEBUG`，`debug` 拿到 `-g`，`-std=c++17` 两边都有（来自 `base` 的 `CMAKE_CXX_STANDARD`）。这就把 preset 里写的 `CMAKE_BUILD_TYPE: Debug/Release` 和实际编译器参数之间的因果链坐实了。两个二进制跑出来的输出也对得上：

```text
$ ./build/debug/app
debug build (NDEBUG NOT defined)

$ ./build/release/app
release build (NDEBUG defined)
```

一份 `CMakePresets.json`，两个 preset，两条独立的构建树，两种行为不同的二进制，命令只有 `cmake --preset debug` / `cmake --preset release` 这么短。和前面那条 130 多字符的 `-D` 老式命令对比，差距摆在这儿。

## CMakeUserPresets.json：个人覆盖

`CMakePresets.json` 是团队共享的，进版本控制。但有些东西天生就是「本机独有」——比如 vcpkg 装在哪个目录、本地是不是开了 ASan、笔者自己想加一个临时的 preset 试验某个 flag。这些写进 `CMakePresets.json` 会污染团队配置，别人 pull 下来要么路径找不到、要么莫名其妙开了不该开的选项。

CMake 给的解法是 `CMakeUserPresets.json`。它和 `CMakePresets.json` 放同一个目录，结构完全一样，但语义是「个人覆盖」：

```text
项目根/
├── CMakePresets.json        # 进 git,团队共享
├── CMakeUserPresets.json    # 进 .gitignore,只在本机
├── CMakeLists.txt
└── ...
```

`CMakeUserPresets.json` 里定义的 preset 和主文件里的 preset 会被合并展示。更关键的是，**UserPresets 里的 preset 能继承主文件里的 hidden preset**。笔者在本机加了一个 `asan` preset，继承主文件里的 `base`，叠一层 ASan flag：

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "asan",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-fsanitize=address -fno-omit-frame-pointer"
            }
        }
    ]
}
```

再 `--list-presets` 看看：

```text
$ cmake --list-presets
Available configure presets:

  "asan"
  "debug"   - Debug (含 -g -O0)
  "release" - Release (含 -O3 -DNDEBUG)
```

`asan` 出现了，跟 `debug`、`release` 平起平坐。直接 `cmake --preset asan` 跑通，构建目录自动落到 `build/asan`：

```text
$ cmake --preset asan
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-presets-demo/build/asan
```

::: warning CMakeUserPresets.json 必须进 .gitignore
官方文档原话是「should NOT be checked in」。它的存在前提就是「每台机器路径不同」，进了 git 必然冲突。新建工程时第一件事就是把 `CMakeUserPresets.json` 加进 `.gitignore`，别等同事 PR 里带着他自己的 vcpkg 路径来折磨您。
:::

## IDE 集成：VSCode CMake Tools

命令行之外，preset 真正落地的地方往往是 IDE。VSCode 的 CMake Tools 扩展原生读 `CMakePresets.json`，状态栏直接列出可选的 configure preset 和 build preset，点一下就切换，不用敲命令。

clangd 也间接受益。CMake Tools 选了 preset 之后会自动跑对应的 configure，生成的 `compile_commands.json` 会被 clangd 拉去给编辑器做补全和跳转。preset 里固化了所有 `-D` 和 Generator，意味着 IDE 里看到的编译环境跟命令行、跟 CI 完全一致——这是 preset 相比「IDE 自己管一套配置」最大的优势：单一数据源。

Remote-WSL 场景下也顺手：`CMakePresets.json` 跟着源码进 WSL 文件系统，VSCode Remote 端的 CMake Tools 直接读，不用在 Windows 端和 WSL 端各配一遍。

## 衔接交叉编译

到这里您可能已经嗅到 preset 和交叉编译的天然契合。交叉编译的核心就是 `-DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake` 这条 `-D`，再加上一堆和目标板相关的缓存变量。这些恰好是 preset 最擅长固化的东西。

`CMakePresets.json` 给了专门的 `toolchainFile` 字段，比塞进 `cacheVariables` 更规范：

```json
{
    "name": "f407-debug",
    "inherits": "base",
    "toolchainFile": "${sourceDir}/cmake/arm-none-eabi-gcc.cmake",
    "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "ARM_CORTEX_M": "M4F"
    }
}
```

之后 `cmake --preset f407-debug` 一行就完成交叉编译配置，团队里任何人 pull 下来都能复现同一份工具链设定。这个机制怎么和 `arm-none-eabi-g++`、sysroot、cortex-m 链接脚本配合，咱们在 vol7 交叉编译篇详细展开。

## 配套示例

本文的工程脚手架可以从仓库示例目录直接跑：

```text
code/examples/vol7/cmake-fundamentals/04-presets/
├── CMakeLists.txt
├── main.cpp
└── CMakePresets.json
```

进入该目录后，依次 `cmake --list-presets`、`cmake --preset debug`、`cmake --build --preset debug`、`./build/debug/app`，即可复现本文全部输出。想验证 `cacheVariables` 的传播，把 `debug` 改成 `release` 重跑一遍，对比 `build/release/build.ninja` 里的 `FLAGS = -O3 -DNDEBUG` 和 `build/debug/build.ninja` 里的 `FLAGS = -g`。

到这里咱们把 preset 的结构、hidden+inherits 组合、CMakeUserPresets.json 个人覆盖、IDE 集成都落到了实处，也用真实输出验证了 `${presetName}` 宏展开和 `CMAKE_BUILD_TYPE` 传播。下一篇要解决一个 vol7 一直欠着的问题：当目标板从 x86 Linux 换成 STM32F407 这种 ARM Cortex-M 设备时，`CMakeLists.txt` 怎么写、toolchain 文件长什么样、preset 怎么和它们挂上钩——也就是交叉编译的完整链路。
