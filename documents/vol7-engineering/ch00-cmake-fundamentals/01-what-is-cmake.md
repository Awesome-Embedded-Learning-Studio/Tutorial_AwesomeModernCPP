---
title: "CMake 是什么——构建系统生成器的两段式流水线"
description: "讲透 CMake 作为构建系统生成器的定位，configure 与 build 两段式流水线各自干什么，以及 Make/Ninja/Visual Studio 三种 Generator 怎么选"
chapter: 7
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 18
prerequisites:
  - "vol1 ch00: 第一个程序"
related:
  - "交叉编译与 CMake"
  - "编译器选项"
---

# CMake 是什么——构建系统生成器的两段式流水线

卷一第一个程序那篇，咱们点过 CMake 的名字，也照着抄过五行 `CMakeLists.txt` 把工程跑通。当时笔者留了一句「我们这里先用 `g++`，等后面的章节再正式引入 CMake」(03-first-program.md 第 119 行)。这句话欠了好几卷，这一篇正是来兑现的。

但今天不是来教您怎么敲命令的——`cmake -B build` 谁不会敲。咱们要搞清楚的是 CMake 在背后到底干了什么：为什么有「配置」和「生成」两步、它跟 `g++` 到底是什么关系、为什么同一个工程既能产出 Makefile 又能产出 `build.ninja`。把这些想透，后面学 target、学 `find_package`、学交叉编译才不会觉得是在背咒语。

## CMake 不是编译器，它是构建系统生成器

这一节先把最根本的误解拆掉。

很多人第一次接触 CMake 的反应是「它是个编译器」或者「它替代了 `g++`」。都不是。CMake 自己一行代码都不编译。它真正干的事情是：读您写的 `CMakeLists.txt`，根据当前平台和工具链，生成别的构建系统文件——Makefile、`build.ninja`、Visual Studio 的 `.sln`——然后由 Make、Ninja 或 MSBuild 这些「真正的构建工具」去调编译器。

一句话：**CMake 生成能编译代码的文件。** 它是构建系统之上的一层，业内叫「构建系统生成器」(build system generator)，或者换个说法叫「元构建系统」(meta build system)。

::: details 元构建系统这个词哪来的
普通构建系统(Make/Ninja)直接描述「编译哪些源文件、怎么链接」。元构建系统再往上一层：它不直接描述编译过程，而是描述「这个项目的结构是什么」，再根据您当前选的工具链，翻译成对应构建系统能读的文件。CMake、Meson、Bazel 都属于这一层。
:::

为什么 C++ 比 Rust 和 Go 多出来这么一层？根子在 ISO。C++ 标准委员会只管标准语言本身（语法、标准库），从来不管工具链怎么组织、构建文件长什么样。结果就是 Windows 上 MSVC 一套、Linux 上 GCC 一套、嵌入式平台 `arm-none-eabi-g++` 又一套，每家都有自己的编译选项和工程格式。Rust 和 Go 是「语言 + 官方工具链(`cargo`/`go`)」打包发行的，根本没这个问题。

CMake 解决的就是这个历史遗留：让您写一份 `CMakeLists.txt`，它在 Windows 上吐 Visual Studio 工程，在 Linux 上吐 Makefile，在想要速度的机器上吐 Ninja。源码描述一份，构建文件因地制宜。

## 两段式流水线：configure 和 build

理解了 CMake 的定位，那个让人困惑的「为什么 CMake 要敲两次命令」就顺理成章了。CMake 的工作流天然分成两段。

第一段叫 **configure(配置)**。这一段里 CMake 读您的 `CMakeLists.txt`，检测编译器在哪、能不能跑、什么版本，把结果记进 `CMakeCache.txt`，最后生成构建文件。注意：这一段**没有编译您的一行代码**。它只是在「搭脚手架」。

第二段叫 **build(构建)**。这一段才真正调编译器，把源文件一个个编成 `.o`、再链接成可执行文件或库。

咱们来看真实的 configure 输出。下面是笔者在本机(GCC 16.1.1、CMake 4.4.0)对一个最小工程跑 `cmake -B build -G Ninja` 的结果：

```text
$ cmake -B build -G Ninja
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-demo/build
```

逐行看。前六行全是 CMake 在「摸底」：识别编译器版本(`GNU 16.1.1`)、探 ABI 信息、确认编译器能正常工作、收集它支持的编译特性。这些信息后面要用——比如您在 `CMakeLists.txt` 里写了 `set(CMAKE_CXX_STANDARD 17)`，CMake 就得知道当前编译器到底支不支持 C++17，不支持就立刻报错给您。然后 `Configuring done` 表示摸底完成、`Generating done` 表示构建文件已经写盘，最后一行告诉您文件落在哪。

注意整个过程没有一行 `Building CXX`。这就是 configure：它只搭台，不唱戏。

再看 build。`cmake --build build` 是统一入口，不管底层是 Make 还是 Ninja 都这么敲：

```text
$ cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[2/2] Linking CXX executable hello
```

`[1/2]`、`[2/2]` 是 Ninja 的进度标记，意思是「两步里的第一步、第二步」。第一步编译 `main.cpp` 成目标文件，第二步链接成 `hello`。这才是真正在跑 `g++`。

为什么非要分两步？关键在**性能特征不一样**。configure 慢——它要重启进程、重新摸底、重新生成所有构建文件。但 configure 只在您改了 `CMakeLists.txt`、加新文件、切换 Generator 时才需要重跑。build 快——它做的是增量编译，只重编动过的文件。所以日常开发循环里，configure 偶尔跑一次，build 跑无数次。

咱们用同一个最小工程实测一下，看缓存对 configure 速度的影响：

```text
$ rm -rf build && time cmake -B build -G Ninja > /dev/null
cmake -B build -G Ninja > /dev/null  0.09s user 0.08s system 93% cpu 0.183 total

$ time cmake -B build -G Ninja
-- Configuring done (0.0s)
-- Generating done (0.0s)
cmake -B build -G Ninja  0.01s user 0.00s system 90% cpu 0.017 total
```

冷启 configure 0.183 秒，命中缓存的二次 configure 只要 0.017 秒，差了十倍。这个最小工程体量太小看不出感觉，但真实工程里第一次 configure 要十几秒、二次只要零点几秒是常态。这就是为什么把 configure 拆出来、配上缓存是有意义的——不然每次编一行代码都得重新摸一遍底，谁受得了。

## Generator：选 Make 还是 Ninja

CMake 把「生成哪种构建文件」这件事抽象成了一个叫 **Generator(生成器)** 的概念。您在 configure 时通过 `-G` 参数选一个，CMake 就生成对应那一套文件。

三个最常用的 Generator：

Unix Makefiles 是默认选项，Linux/macOS 上不指定 `-G` 就是它。它生成 `Makefile`，由 `make` 命令驱动构建。最老牌、最通用、所有 Unix 系统都自带 `make`。缺点是慢——`make` 是上世纪七十年代的设计，依赖检查和并行调度都不够现代。

Ninja 是现代推荐选项。它生成 `build.ninja`，由 `ninja` 命令驱动。Ninja 是专门为「被元构建系统生成」而设计的，启动开销小、并行调度激进、增量构建快。代价是要单独装一份 `ninja`(包名通常就叫 `ninja` 或 `ninja-build`)。

Visual Studio 是 Windows 上 IDE 集成用的选项(`-G "Visual Studio 17 2022"`)。它生成 `.sln` 和 `.vcxproj`，能直接在 Visual Studio 里打开、F5 调试。不追求 IDE 体验的话，Windows 上一样能用 Ninja。

命令差异就一个 `-G` 参数。下面对同一个工程分别用两种 Generator 跑一遍，看它们各自生成什么：

```text
cmake -B build      -G Ninja            # 选 Ninja
cmake -B build-make -G "Unix Makefiles" # 选 Make
```

两种 configure 命令的输出长得几乎一样(都是那段「检测编译器、Configuring done」)，区别在生成出来的构建文件不一样。咱们直接对比两个 `build/` 目录下的产物：

```text
$ ls build/          # Ninja 生成的
build.ninja
cmake_install.cmake
CMakeCache.txt
CMakeFiles
hello

$ ls build-make/     # Make 生成的
cmake_install.cmake
CMakeCache.txt
CMakeFiles
hello
Makefile
```

Ninja 那边多了个 `build.ninja`，Make 这边多了个 `Makefile`。`CMakeCache.txt`、`CMakeFiles/`、`cmake_install.cmake` 两边都有，是 CMake 自己的基础设施。

笔者的建议：本机开发一律默认 Ninja。它快得不只是一点点，而且 `build.ninja` 比 `Makefile` 简洁得多(您 `cat` 一下两份文件就知道)。除非环境里装不上 `ninja`，否则没理由退回 Make。后面讲到交叉编译、CI 时，Ninja 也是更顺手的选择。

## out-of-source 构建：别污染源码目录

CMake 默认推荐一种构建方式叫 **out-of-source 构建**(分离源码树和构建树)。意思是：所有构建产物——目标文件、可执行文件、`CMakeCache.txt`、生成的构建文件——都堆进一个 `build/` 子目录，源码目录保持干净。

`cmake -B build` 这个 `-B` 参数就是干这个的，它告诉 CMake「构建树放 `build/` 下」。目录结构长这样：

```text
cmake-demo/
├── CMakeLists.txt      # 您写的，进 git
├── main.cpp            # 您写的，进 git
└── build/              # CMake 生成的，进 .gitignore
    ├── CMakeCache.txt
    ├── CMakeFiles/
    ├── build.ninja
    ├── cmake_install.cmake
    └── hello           # 最终可执行文件
```

这种结构的好处很直接：源码目录里看不到一个 `.o` 文件，看不到 `a.out`，看不到临时产物。想清掉重来？`rm -rf build/` 一条命令，源码纹丝不动。想发版打包？源码目录里全是干净的源文件，不用费劲挑出哪些该归档哪些不该。

::: details in-source 构建也能跑，但别这么干
CMake 也允许直接在源码目录里 `cmake .`(叫 in-source 构建)，会直接在当前目录生成 Makefile 和一堆 `CMakeFiles/`。看起来省事，但一旦源码目录被污染，`git status` 一片红，清理起来要逐个找。CMake 新版本对此还有限制：默认拒绝在同一目录二次 configure，避免把源码树搞乱。养成 `-B build` 的习惯，省得后面受罪。
:::

这里要专门说说 **`CMakeCache.txt`** 这个文件。它就是前面那段「冷启 vs 缓存」里加速二次 configure 的关键。第一次 configure 时，CMake 把所有摸底结果存进去：编译器路径、编译器版本、Generator 选择、您通过 `-D` 设的变量、各种特性检测结果。下一次 configure 时，CMake 先读缓存，没变的东西直接复用，省掉重新摸底的时间。

打开看看，里面是键值对格式，关键字段长这样：

```text
//Path to CXX compiler.
CMAKE_CXX_COMPILER:FILEPATH=/usr/sbin/c++

//Name of CMake project.
CMAKE_PROJECT_NAME:STATIC=hello_cmake

//Name of generator.
CMAKE_GENERATOR:INTERNAL=Ninja
```

注意 `CMAKE_GENERATOR` 这一行——它记住了您选的 Generator。所以二次 configure 不用再写 `-G Ninja`，CMake 自己知道接着用 Ninja。这也是为什么有时候您想换 Generator 时，光敲 `-G` 没用、CMake 还在用旧的——`CMakeCache.txt` 把它锁住了，得 `rm -rf build/` 清掉重来。

`CMakeCache.txt` 进 git 吗？绝对不进。它跟机器环境强绑定(编译器路径、绝对路径都在里头)，进了 git 保证每台机器都冲突。`build/` 整个目录进 `.gitignore`，一了百了。

## 最小工程三件套

讲完原理，咱们落到一个能跑的最小工程上。一个合法的 `CMakeLists.txt` 至少要有三行：

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello_cmake LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello main.cpp)
```

第一行 `cmake_minimum_required(VERSION 3.20)` 声明本工程需要的最低 CMake 版本。低于这个版本的 CMake 看到这行直接报错退出。这一行的真正作用不只是版本检查——它还会触发 CMake 进入对应版本的「策略兼容模式」(policy)，确保新版本 CMake 的行为变更不会悄悄影响老工程。`3.20` 是个稳妥的下限，2021 年发布，主流发行版都能装上。

第二行 `project(hello_cmake LANGUAGES CXX)` 给工程起名 `hello_cmake`，并声明要用 C++(`CXX`)。`project()` 这一行触发的就是前面 configure 输出里那段「检测编译器、探 ABI」的摸底——`LANGUAGES CXX` 告诉 CMake「我需要 C++ 编译器」，CMake 才会去满世界找 `g++`/`clang++`/`MSVC`。

第三行 `add_executable(hello main.cpp)` 是真正告诉 CMake「造一个叫 `hello` 的可执行文件，源码是 `main.cpp`」。这一行配置进 `build.ninja` 后，build 阶段就会变成 `g++ main.cpp -o hello`。

中间那两行 `set(CMAKE_CXX_STANDARD 17)` 和 `set(CMAKE_CXX_STANDARD_REQUIRED ON)` 是设置默认 C++ 标准。前者要求用 C++17 编译，后者要求「编译器不支持就报错而不是降级」。这两行以后讲到 target 时还会回来——更现代的写法是 `target_compile_features()`，现在先这么写够用。

配套的 `main.cpp`：

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, CMake!\n";
    return 0;
}
```

三步命令跑通整个流水线：

```text
$ cmake -B build -G Ninja && cmake --build build && ./build/hello
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-demo/build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[2/2] Linking CXX executable hello
Hello, CMake!
```

最后一行 `Hello, CMake!` 就是 `./build/hello` 跑出来的。第一条命令搭脚手架、第二条命令真正编译链接、第三条命令执行。这条流水线以后不论工程多大，骨架都一样。

## 配套示例

本文的最小工程可以从仓库示例目录直接跑：

```text
code/examples/vol7/cmake-fundamentals/01-what-is-cmake/
├── CMakeLists.txt
└── main.cpp
```

进入该目录后，照搬上面那三步命令即可复现全部输出。

到这里咱们把 CMake 的定位、两段式流水线、Generator 选择、out-of-source 构建讲透了，也跑通了最小工程。下一篇要解决一个更实际的问题：当工程不止一个 `main.cpp`、还要拆成多个模块、还要复用第三方库的时候，怎么管理「这个 target 用哪些头文件、链接哪个库、开什么编译选项」。这就引出 CMake 的核心心智模型——**target**——以及为什么不该再用全局的 `include_directories()` 这种「命令式」写法，而该转向 `target_include_directories()` 这种「面向对象」的写法。
