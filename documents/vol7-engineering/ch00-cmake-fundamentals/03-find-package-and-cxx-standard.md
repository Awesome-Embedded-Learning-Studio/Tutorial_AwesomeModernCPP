---
title: "依赖与 C++ 标准——find_package 和 cxx_std_NN 的现代写法"
description: "讲透 C++ 标准的三种设法为什么手动塞 flag 是反模式，find_package 怎么通过导入 target 把第三方库的使用需求带过来，以及找不到包时怎么排查"
chapter: 7
order: 3
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
  - "vol7 ch00 02: Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求"
related:
  - "CMakePresets.json——从 cmake -D 老式到 --preset 可复现"
  - "交叉编译与 CMake"
---

# 依赖与 C++ 标准——find_package 和 cxx_std_NN 的现代写法

上一篇咱们把 target 和使用需求讲透了，PUBLIC/PRIVATE/INTERFACE 三态怎么沿链接图传播也跑过实测。这一篇接两个工程里最常踩的具体问题：怎么告诉 CMake 您要用 C++20，怎么把第三方库链接进来。这俩问题网上一搜全是答案，但老式写法还在大量教程里流传，照抄会埋坑。咱们把每种设法都跑一遍，看清为什么有的写法该扔进故纸堆。

## C++ 标准的三种设法，哪个对

设 C++ 标准这事，CMake 圈子里能看到三种写法同时存在。咱们一个个来，先把代码摆出来再讲为什么。

第一种，绑在 target 上：

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
```

第二种，目录级变量，起步卷 [getting-started/04](/getting-started/04-multi-file-cmake) 里咱们就是用这个把工程跑起来的：

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

第三种，直接往编译标志里塞 `-std=c++20`：

```cmake
string(APPEND CMAKE_CXX_FLAGS " -std=c++20")   # 反模式,别抄
```

前两种 CMake 官方都认，第三种是反模式。下面用实测数据说清楚为什么。

### `target_compile_features` 是「最低要求」语义

`target_compile_features(app PRIVATE cxx_std_20)` 这一行翻译成人话：app 这个 target 编译时，C++ 标准不能低于 C++20。注意措辞，是「不能低于」，不是「必须正好等于」。

这个语义很关键，CMake 拿到这条要求后会拿编译器的默认标准去做对比，根据对比结果决定要不要往编译命令里塞 `-std` 标志。咱们用 GCC 16.1.1 实测一遍，它默认标准是 `gnu++20`（用 `g++ -dM -E -x c++ /dev/null | grep __cplusplus` 能看到 `202002L`，对应 C++20）。下面这份 `CMakeLists.txt` 造了三个 target，分别要求 17、20、23：

```cmake
cmake_minimum_required(VERSION 3.20)
project(feat_test LANGUAGES CXX)

add_executable(app_cxx17 main.cpp)
target_compile_features(app_cxx17 PRIVATE cxx_std_17)

add_executable(app_cxx20 main.cpp)
target_compile_features(app_cxx20 PRIVATE cxx_std_20)

add_executable(app_cxx23 main.cpp)
target_compile_features(app_cxx23 PRIVATE cxx_std_23)
```

用 Make 这个 Generator，开 `CMAKE_VERBOSE_MAKEFILE`，看每个 target 实际发到 `g++` 的命令：

```text
$ cmake -S . -B build -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=ON > /dev/null
$ cmake --build build --target app_cxx17 2>&1 | grep "/c++"
/usr/sbin/c++    -MD -MT ... -c .../main.cpp
$ cmake --build build --target app_cxx20 2>&1 | grep "/c++"
/usr/sbin/c++    -MD -MT ... -c .../main.cpp
$ cmake --build build --target app_cxx23 2>&1 | grep "/c++"
/usr/sbin/c++   -std=gnu++23 -MD -MT ... -c .../main.cpp
```

逐字读。要求 17 的 target，编译命令里没有 `-std`：因为编译器默认已经是 20，比 17 高，CMake 判定要求满足，不再加标志。要求 20 的也没有：默认就是 20，正中下怀。要求 23 的才冒出一条 `-std=gnu++23`：默认 20 不够，CMake 主动给升到 23。

这就是「最低要求」语义的妙处。您写 `cxx_std_20` 是在声明「这份代码用了 C++20 特性，低于 20 编不过」，CMake 会按需补标志，绝不会把默认的 20 偷偷降到 17。换台默认是 `gnu++17` 的老编译器（比如 GCC 11），同样一份 `CMakeLists.txt`，CMake 就会自动给加 `-std=gnu++20`。一份配置，跨编译器版本都能拿到正确的标准。

::: details 那个 gnu++ 是什么，能去掉吗
`gnu++20` 是 GCC 的「C++20 加 GNU 扩展」方言，对应纯标准的写法是 `c++20`。两者区别是前者允许用 `typeof`、零长数组这些 GCC 私货，写出来的代码可移植性差。CMake 默认走 `gnu++NN` 是为了兼容老代码，您可以在 target 上设 `CXX_EXTENSIONS OFF` 把它逼回纯 `c++NN`：

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_23)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
```

实测，同样要求 cxx_std_23，开了 `CXX_EXTENSIONS OFF` 之后编译命令从 `-std=gnu++23` 变成 `-std=c++23`：

```text
$ cmake --build build --target app 2>&1 | grep "/c++"
/usr/sbin/c++   -std=c++23 -MD -MT ... -c .../main.cpp
```

新工程建议默认开 OFF，跨编译器行为更可预测。
:::

`cxx_std_NN` 还能 PUBLIC 出去，复用上一篇讲的使用需求传播机制。一个库自己要求 C++20，下游链接它自动继承这个要求：

```cmake
target_compile_features(mylib PUBLIC cxx_std_20)
```

下游链接 `mylib` 时，CMake 看到 `INTERFACE_COMPILE_FEATURES` 里有 `cxx_std_20`，会自动给下游也提一级标准。这是 target 级写法相对目录级写法的最大优势：标准要求跟着 target 走，沿依赖图自动传，您不用在每个下游工程里再写一遍 `set(CMAKE_CXX_STANDARD 20)`。

### 目录级写法 `set(CMAKE_CXX_STANDARD)`：能用，但有上限

第二种写法咱们在起步卷用过，长这样：

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

这三行的作用域是「当前 `CMakeLists.txt` 及其子目录里的所有 target」，本质上是给目录下所有 target 的 `CXX_STANDARD` 属性赋默认值。`CMAKE_CXX_STANDARD_REQUIRED ON` 是必须的，它告诉 CMake「编译器达不到这个标准就报错」，否则编译器太老时 CMake 会偷偷降级编过去，编过了但行为不对，调试半天才发现根因在这。

::: warning 漏写 `CMAKE_CXX_STANDARD_REQUIRED ON` 会偷偷降级
CMake 默认 `CMAKE_CXX_STANDARD_REQUIRED` 是 `OFF`，意思是「编译器不支持这个标准也尽量编」。结果是您写 `set(CMAKE_CXX_STANDARD 20)`，编译器最高只到 17 时，CMake 不报错，默默拿 17 编下去。您用了 C++20 的 `concept`、模板 lambda，编不过才报错，但报错信息不会指向「标准被降级」，而是指向具体的语法行，绕一大圈才查到根因。所以 `CMAKE_CXX_STANDARD` 和 `CMAKE_CXX_STANDARD_REQUIRED ON` 必须配套写。
:::

这种写法目前还能接受，因为绝大多数工程的 C++ 标准就是「全工程统一」一个值。但它有两处不如 target 级写法：一是它不会随 target 传播，下游链接您的库，标准要求不会自动过去；二是它的作用域是目录级，本质上跟上一篇讲的 `include_directories()` 一样属于全局设置，工程一复杂就不够精确。

迁移建议：新工程优先用 `target_compile_features(mylib PUBLIC cxx_std_NN)`，把标准也变成 target 的使用需求；老工程继续用 `set(CMAKE_CXX_STANDARD)` 不影响功能，等下次重构再换。

### 手动塞 `-std=c++20`：反模式，别这么写

第三种写法看着最「直接」，网上老教程里随处可见：

```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")
# 或者
string(APPEND CMAKE_CXX_FLAGS " -std=c++20")
add_executable(app main.cpp)
```

实测它确实能让编译命令里出现 `-std=c++20`：

```text
$ cmake -S . -B build -G "Unix Makefiles" -DCMAKE_VERBOSE_MAKEFILE=ON > /dev/null
$ cmake --build build --target app 2>&1 | grep "/c++"
/usr/sbin/c++   -std=c++20 -MD -MT ... -c .../main.cpp
```

看着没问题，问题全在背后。首先这一行绕过了 CMake 的标准管理。CMake 内部维护着一份「编译器认识哪些标准、每个标准对应哪个标志」的对照表，`target_compile_features` 和 `set(CMAKE_CXX_STANDARD)` 都走这张表。您手动塞 `-std=c++20`，CMake 不知道您设了标准，于是 `CMAKE_CXX_STANDARD` 这个变量还是空，下游想读它来做判断就拿到空值，依赖图里的标准传播整条链断掉。

其次它跨平台不一致。GCC 和 Clang 用 `-std=c++20`，MSVC 用 `/std:c++20`， flags 写死了 GCC 风格，换个 MSVC 工程这套就编译报错。CMake 的标准管理替您抹平了这个差异，您只要写 `cxx_std_20`，CMake 自己挑对应平台的标志。

最后它跟 `CXX_EXTENSIONS`、`CMAKE_CXX_STANDARD_REQUIRED` 这些机制完全脱节，等于绕过整套抽象自己手搓。一份 `CMakeLists.txt` 里只要出现一行 `set(CMAKE_CXX_FLAGS ... -std=...)`，就标志着这份配置还是老式 CMake 思维。

迁移规则：把所有手动塞 `-std=` 的地方删掉，换成上面第一种或第二种写法。

## find_package：怎么链接第三方库

C++ 标准的事清楚了，接下来讲第三方库。真实工程里 `fmt` 这种库不是咱们手写的，要从系统或包管理器里接进来，CMake 给的命令是 `find_package`。

现代写法两行就完事：

```cmake
find_package(fmt REQUIRED)
target_link_libraries(app PRIVATE fmt::fmt)
```

`find_package(fmt REQUIRED)` 干的事是去几个标准位置（`CMAKE_PREFIX_PATH` 下的 `<prefix>/lib/cmake/fmt/` 等）找一份 `fmt-config.cmake`（也叫包配置文件），找到后执行它。这个配置文件是 fmt 自己装的，它知道 fmt 的头文件在哪、库文件在哪、链接需要哪些编译选项。执行完之后，您的工程里凭空多出一个叫 `fmt::fmt` 的 target。

这个 `fmt::fmt` 是个**导入 target**（imported target）。导入 target 跟咱们上一篇讲的普通 target 不一样，它不是您工程里造出来的，是别人造好了打包进 config 文件、`find_package` 时搬进来的。它身上同样挂着 `INTERFACE_INCLUDE_DIRECTORIES`、`INTERFACE_COMPILE_DEFINITIONS`、`IMPORTED_LOCATION` 这些使用需求属性。您 `target_link_libraries(app PRIVATE fmt::fmt)` 一链接，这些属性就像上一篇讲的 PUBLIC 那样自动流到 `app` 上。

咱们扒一个真实的 fmt::fmt 看看。本机装了 fmt 12.2.0，它的 config 文件在 `/usr/lib/cmake/fmt/fmt-config.cmake`，里面创建 `fmt::fmt` 的关键几行（来自 `fmt-targets.cmake`）长这样：

```cmake
add_library(fmt::fmt SHARED IMPORTED)
set_target_properties(fmt::fmt PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include"
  ...
)
```

`SHARED IMPORTED` 告诉 CMake 这是个动态库的导入 target。`INTERFACE_INCLUDE_DIRECTORIES` 是它的公开头文件路径。下面这份 `CMakeLists.txt` 找到 fmt 之后，把它的几个属性打出来：

```cmake
find_package(fmt REQUIRED)
foreach(prop TYPE INTERFACE_INCLUDE_DIRECTORIES INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_FEATURES)
    get_target_property(v fmt::fmt ${prop})
    message(STATUS "fmt::fmt.${prop} = ${v}")
endforeach()
```

跑一次 configure：

```text
$ cmake -S . -B build
-- fmt::fmt.TYPE = SHARED_LIBRARY
-- fmt::fmt.INTERFACE_INCLUDE_DIRECTORIES = /usr/include
-- fmt::fmt.INTERFACE_COMPILE_DEFINITIONS = FMT_SHARED
-- fmt::fmt.INTERFACE_COMPILE_FEATURES = cxx_std_11
```

逐条读。`TYPE` 是 SHARED_LIBRARY，是动态库。`INTERFACE_INCLUDE_DIRECTORIES` 是 `/usr/include`，下游包含 `<fmt/core.h>` 的路径来源。`INTERFACE_COMPILE_DEFINITIONS` 是 `FMT_SHARED`，这是个关键信号，因为 fmt 编成动态库时，下游链接它必须定义 `FMT_SHARED` 才能正确导入符号。`INTERFACE_COMPILE_FEATURES` 是 `cxx_std_11`，fmt 自己声明它至少要 C++11。

您一行 `target_link_libraries(app PRIVATE fmt::fmt)` 链上去，这四条属性自动变成 `app` 的编译环境。咱们看 `app` 实际拿到的编译命令：

```text
$ cmake -S . -B build -G Ninja > /dev/null && cmake --build build -v 2>&1 | grep "/c++"
[1/2] /usr/sbin/c++ -DFMT_SHARED   -MD -MT ... -c .../main.cpp
```

注意命令里凭空冒出来的 `-DFMT_SHARED`。`app` 的 `CMakeLists.txt` 里没写过这一行，它来自 `fmt::fmt` 的 `INTERFACE_COMPILE_DEFINITIONS`。`/usr/include` 是系统默认路径所以没显式出现在命令里，但您把 fmt 装到非标准路径（比如 `/opt/fmt`），那条 `-I/opt/fmt/include` 就会自动冒出来。链接阶段也一样，看实际链接命令：

```text
[2/2] : && /usr/sbin/c++ ... CMakeFiles/app.dir/main.cpp.o -o app  /usr/lib/libfmt.so.12.2.0  && :
```

`/usr/lib/libfmt.so.12.2.0` 是 `fmt::fmt` 的 `IMPORTED_LOCATION` 解析出来的真实库文件路径。CMake 全程替您把「找头文件、传编译宏、找库文件」这套脏活干完，您只管写一个 `fmt::fmt` 的名字。

这就是导入 target 相对老式写法的根本优势：它把库的「使用需求」打包成一个对象，您链接一次，所有该带的配置自动到位，库升级换路径您也不用改一行代码。

### 老式写法：`${fmt_INCLUDE_DIRS}` 变量风格

网上老教程里常见的另一种写法长这样：

```cmake
find_package(fmt REQUIRED)
include_directories(${fmt_INCLUDE_DIRS})                      # 反模式
add_executable(app main.cpp)
target_link_libraries(app ${fmt_LIBRARIES})                   # 反模式
```

`include_directories(${fmt_INCLUDE_DIRS})` 是上一篇讲过的目录级全局命令，污染当前目录下所有 target。`${fmt_LIBRARIES}` 这种变量写法依赖 config 文件把库列表写进一个变量，您手工读出来再传给 `target_link_libraries`。问题在于这种写法完全不传播使用需求：`fmt_LIBRARIES` 只是个库名列表，不带 `-DFMT_SHARED`，不带 `INTERFACE_INCLUDE_DIRECTORIES`，不带 `cxx_std_11`，您漏一个就编不过或者行为不对。

更要命的是这套变量名没有统一规范。fmt 用的可能是 `fmt_LIBRARIES`，OpenCV 用的可能是 `OpenCV_LIBS`，Boost 用的可能是 `Boost_LIBRARIES`，您每接一个库就得查它的 config 文件提供哪些变量。导入 target 则统一是 `库名::库名` 的命名空间，您只要在文档里找到这个带 `::` 的名字，链接一次就齐活。

迁移规则：把所有 `include_directories(${X_INCLUDE_DIRS})` 删掉，把所有 `target_link_libraries(app ${X_LIBRARIES})` 改成 `target_link_libraries(app PRIVATE X::X)`。前提是您接的库 config 文件提供了导入 target，现在主流库（fmt、spdlog、Catch2、nlohmann_json 等）都提供。

::: warning 库没提供导入 target 怎么办
少数老库或者自己手写的 config 文件可能只提供 `${X_INCLUDE_DIRS}` 变量、没有 `X::X` 这种导入 target。这种情况下您有两个选择。一是自己造一个 INTERFACE 库当封装：

```cmake
find_package(OldLib REQUIRED)
add_library(OldLib::OldLib ALIAS OldLib::OldLib)   # 不行,OldLib 不是个 target
# 正确做法:造一个 interface target 把变量包进去
add_library(oldlib_wrapper INTERFACE)
target_include_directories(oldlib_wrapper INTERFACE ${OldLib_INCLUDE_DIRS})
target_link_libraries(oldlib_wrapper INTERFACE ${OldLib_LIBRARIES})
target_link_libraries(app PRIVATE oldlib_wrapper)
```

这样下游统一链接 `oldlib_wrapper`，配置从这一处向外传播。二是劝库作者更新 config 文件，或者直接换库。
:::

## 找不到包怎么办

`find_package` 报错时的排查路径是有固定套路的。先看真实报错长什么样。下面这份 `CMakeLists.txt` 找一个根本不存在的库：

```cmake
find_package(NonExistentPkg 9.9.9 REQUIRED)
```

configure 直接挂掉，CMake 报的错是：

```text
CMake Error at CMakeLists.txt:4 (find_package):
  By not providing "FindNonExistentPkg.cmake" in CMAKE_MODULE_PATH this
  project has asked CMake to find a package configuration file provided by
  "NonExistentPkg", but CMake did not find one.

  Could not find a package configuration file provided by "NonExistentPkg"
  (requested version 9.9.9) with any of the following names:

    NonExistentPkg.cps
    nonexistentpkg.cps
    NonExistentPkgConfig.cmake
    nonexistentpkg-config.cmake

  Add the installation prefix of "NonExistentPkg" to CMAKE_PREFIX_PATH or set
  "NonExistentPkg_DIR" to a directory containing one of the above files.

-- Configuring incomplete, errors occurred!
```

这段报错信息量大，咱们拆开看。第一段说「您没在 `CMAKE_MODULE_PATH` 里提供 `FindNonExistentPkg.cmake`」，意思是 CMake 先按「Module 模式」找了一遍自己内置的或您提供的 `FindX.cmake` 文件，没找到。第二段说「`NonExistentPkg` 提供的包配置文件也没找到」，列了它要找的几个文件名，其中 `.cps` 是 CMake 3.29 引入的 CPS（CMake Package Specification）新格式，`.cmake` 是经典格式。第三段给的是排查路径。

按报错提示，`find_package` 找不到包的常见原因有这么几个，按出现频率排：

第一，库根本没装。最常见，先确认系统里到底有没有这个库。Linux 用包管理器查（`apt list --installed | grep fmt`、`pacman -Qs fmt`），Windows 看 `vcpkg list`，macOS 看 `brew list`。没装就先装上，装的时候留意有没有装 `-dev` 或 `-devel` 后缀的开发包，因为有些发行版把运行时库和头文件分开卖，只装运行时库 `find_package` 也找不到。

第二，装了但 `CMAKE_PREFIX_PATH` 没设。库装在非标准路径（自己 `make install` 到 `/opt/fmt`，或者 vcpkg 装在 `~/vcpkg/installed/x64-linux`），CMake 默认只搜 `/usr`、`/usr/local` 这几个标准位置，自然找不到。解决方法是 configure 时加 `-DCMAKE_PREFIX_PATH=/opt/fmt`，或者设环境变量 `CMAKE_PREFIX_PATH`。下一篇讲 CMakePresets 时会把这种 `-D` 固化进 JSON。

第三，vcpkg / Conan 的 toolchain 文件没注入。这两个包管理器装库之后，库都装在它们自己管的目录里（vcpkg 是 `installed/`，Conan 是 `~/.conan2/`），不进系统标准路径。它们给您一个 toolchain 文件，configure 时通过 `-DCMAKE_TOOLCHAIN_FILE=<path>/vcpkg.cmake` 传进去，这个 toolchain 文件会自动把 `CMAKE_PREFIX_PATH` 指向它装的库。忘了挂 toolchain，库装了也白装，`find_package` 还是找不到。这是新手最常踩的坑。

第四，库装了但没提供 config 文件。比如系统装的是 fmt 5.x 这种老版本，那时 fmt 还没提供 `fmt-config.cmake`，只有 `FindFMT.cmake` 这种 Module 模式的查找文件（甚至什么都没有）。这种情况 `find_package(fmt)` 走 Config 模式找不到，您要么升级库，要么自己写一个 `FindX.cmake`，要么用 pkg-config 桥接。

::: details find_package 的两种查找模式
`find_package(X)` 默认走两种模式，先 Module 后 Config。

Module 模式找的是 `FindX.cmake`，文件名以 `Find` 开头。这种文件是 CMake 自己写的（内置了一百多个常见库的 `FindX.cmake`），或者您放在 `CMAKE_MODULE_PATH` 里提供的。老式写法常见，因为当年很多库自己不提供 config 文件，靠 CMake 社区维护的 Module 桥接。

Config 模式找的是 `X-config.cmake` 或 `XConfig.cmake`（CMake 3.29+ 还找 `.cps` 文件），文件名以库名开头。这种文件是库作者自己装的，跟库一起发出来，准确性比社区维护的 Module 高。现代主流库（fmt、spdlog、Catch2、Boost 1.70+ 等）都自带 Config 文件，所以 `find_package` 实际大多走 Config 模式。

CMake 默认先 Module 后 Config，您可以用 `find_package(X CONFIG)` 或 `find_package(X MODULE)` 强制只走一种。新工程建议显式写 `CONFIG`，行为更明确，也避免 CMake 内置的某个老 `FindX.cmake` 抢在库自己的 config 文件前面被找到，行为不一致。
:::

## vcpkg / Conan 一句话衔接

第三方库从哪来，咱们前面一直没说。系统包管理器（apt、pacman、brew）装的库版本老、跨平台不一致、CI 上未必能装，做正经工程一般不用它。C++ 生态里两个主流的包管理器是 vcpkg 和 Conan，它们干的事是替您把库编好、装到自己的目录，然后给您一个 toolchain 文件，让 CMake 的 `find_package` 能找到。

用法上的关键就一行 configure 参数：

```text
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

vcpkg 装的库都在 `<vcpkg-root>/installed/` 下，它的 toolchain 文件会自动把 `CMAKE_PREFIX_PATH` 指过去，于是您工程里 `find_package(fmt REQUIRED)` 照常工作，跟系统装的没区别。Conan 思路类似，生成的 toolchain 文件叫 `conan_toolchain.cmake`。这套机制的细节、manifest 文件怎么写、版本怎么锁、和下一篇 CMakePresets 怎么挂上钩，咱们留给后续包管理专题展开。这里您只要记住一件事：库装好之后，注入 toolchain 文件这一步是 CMake 能找到库的关键。

## 配套示例

本文的示例工程在仓库 `code/examples/vol7/cmake-fundamentals/03-find-package/`，结构如下：

```text
03-find-package/
├── CMakeLists.txt    # target_compile_features + 可选 find_package 段
└── main.cpp          # 用 C++20 模板 lambda 验证标准生效
```

`CMakeLists.txt` 的核心三行：

```cmake
add_executable(app main.cpp)
target_compile_features(app PRIVATE cxx_std_20)
set_target_properties(app PROPERTIES CXX_EXTENSIONS OFF)
```

三步跑通：

```text
$ cmake -S . -B build -G Ninja && cmake --build build && ./build/app
3
ab
```

`main.cpp` 里用了一个 C++20 才有的模板 lambda（`[]<typename T>(T a, T b) { return a + b; }`），用来证明 `cxx_std_20` 真的把标准要求传到了编译命令里。想动手验证「最低要求」语义，把 `cxx_std_20` 改成 `cxx_std_23`，重新 configure，再用 `cmake --build build -v` 看编译命令，您会看到 CMake 自动加了一条 `-std=c++23`。

## 接下来

到这里 C++ 标准的三种设法、`find_package` 的导入 target 机制都落到了代码上。咱们这篇里 configure 命令已经长成这样：

```text
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=/opt/fmt ...
```

`-D` 一长就开始出问题：抄错变量名 configure 不报错默默走空配置、同事之间互相问 vcpkg 路径填啥、CI 里改一个选项 PR 一片红。下一篇讲 `CMakePresets.json`，CMake 3.19 引入的机制把这些散在命令行里的 `-D`、Generator 选择、toolchain 注入统一固化进一个 JSON 文件，命令瘦成 `cmake --preset debug` 一行。
