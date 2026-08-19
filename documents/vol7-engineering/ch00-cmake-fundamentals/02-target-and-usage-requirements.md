---
title: "Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求"
description: "讲透 target 是什么、target_* 命令为什么是成员方法、PUBLIC/PRIVATE/INTERFACE 三态怎么传播，以及为什么目录级命令是反模式"
chapter: 7
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 20
prerequisites:
  - "vol7 ch00 01: CMake 是什么——构建系统生成器的两段式流水线"
related:
  - "交叉编译与 CMake"
  - "编译器选项"
---

# Target 心智模型——把 target 当对象，PUBLIC/PRIVATE/INTERFACE 是使用需求

上一篇咱们跑通了一个最小工程，`CMakeLists.txt` 里就一行真正干活的 `add_executable(hello main.cpp)`。当时它那一行命令制造出来的东西，笔者一直没给名字。这一篇就把这个名字交出来：**target**。

target 这个词，在 CMake 官方文档里反复出现，在所有「现代 CMake」教程里被奉为头号概念，社区还有句口头禅叫 think in targets not variables（围绕 target 想，别围绕变量想）。凭什么现代 CMake 把它捧这么高，又为什么您照着老教程抄的 `include_directories()` 已经是反模式，这篇就把它讲透。这是现代 CMake 和老式 CMake 的分水岭，跨过去，后面看任何 `CMakeLists.txt` 都不会觉得是在背咒语。

## 把 target 当一个对象

target 不是一个抽象比喻，它就是 CMake 内部的一个数据结构。理解它最快的方式，是把它想成一个 C++ 对象。

`add_executable(app main.cpp)` 和 `add_library(mylib STATIC src/mylib.cpp)` 这两行命令，是**构造函数**。它们造出一个 target 对象，给它起名 `app` 或 `mylib`，记下它由哪些源文件构成、要编成可执行还是库。从这一行开始，`app` 和 `mylib` 这两个名字就在 CMake 的世界里「活」了，您后面所有配置都拿这个名字当 handle（句柄）去操作。

造出来之后呢？您要给它加头文件搜索路径、告诉它链接哪个库、开哪些编译选项。这些操作对应的就是一堆 `target_*` 开头的命令：

```cmake
target_include_directories(mylib PUBLIC include)
target_link_libraries(mylib PRIVATE fmt)
target_compile_options(mylib PRIVATE -Wall -Wextra)
target_compile_features(mylib PUBLIC cxx_std_17)
```

这些 `target_*` 命令是**成员方法**。它们干的事情本质都一样：拿着 target 的名字，往这个 target 对象上挂属性。`target_include_directories(mylib PUBLIC include)` 翻译过来就是「给 `mylib` 这个对象，往它的 include 路径属性里塞一个 `include`」。

而 target 身上挂的那些东西（include 路径、链接库列表、编译选项、C++ 标准要求），就是它的**成员变量**。每个 target 各自管各自的，互不打扰。

::: details target 在 CMake 内部到底是什么
严格说，target 是 CMake 维护的一组属性的集合。它身上挂的属性可以在 configure 阶段用 `get_target_property(v mylib INCLUDE_DIRECTORIES)` 取出来看。下面实战那一节就会拿这个命令扒给咱们看，target 在内部不是黑盒。
:::

为什么这套「对象思维」重要？因为它把配置的范围限定死了。`target_include_directories(mylib PUBLIC include)` 只动 `mylib` 一个 target 的属性，不影响工程里其他任何 target。这正是接下来要讲的核心区别：老式 CMake 是「全局污染」，现代 CMake 是「target 私有」。

## 使用需求：PUBLIC/PRIVATE/INTERFACE 三态

光有 target 这个对象还不够。真正让现代 CMake 脱胎换骨的，是它对**使用需求（usage requirements）**的建模。这个词听着玄，其实就一句话：一个 target 在被自己编译时、和被别人链接时，要求的配置可能不一样。CMake 用三个关键字把这两种情况区分开。

PRIVATE 表示「我自己编译要用，但别人链接我不需要」。比如 `mylib` 内部实现里调用了第三方库 `fmt` 做字符串格式化，但 `mylib` 的公开头文件里完全看不到 `fmt` 的痕迹，下游链接 `mylib` 的人根本不知道 `fmt` 存在，自然也不需要 `fmt` 的头文件路径。这时候 `fmt` 对 `mylib` 就是 PRIVATE。

INTERFACE 表示「我自己不用，但别人链接我需要」。一个典型场景是 header-only 库（纯头文件库），它自己没有 `.cpp` 要编译，所以「自己用」这条是空的；但下游只要包含它的头文件就得有对应的 include 路径和 C++ 标准要求。这时候所有配置都进 INTERFACE。

PUBLIC 表示「两者都要，自己用加上别人也需要」。最常见的就是公开头文件里直接出现的类型。比如 `mylib.h` 的返回类型是 `std::string`，那下游链接 `mylib` 之后，编译器为了解析这个返回类型，必须能找到 `<string>` 所在的 include 路径。这条路径 `mylib` 自己编 `.cpp` 时要用，下游链接 `mylib` 时也要用，这就是 PUBLIC。

把这三态的「自己用 / 别人用」拆开来，背后是一个简单的事实表：

| 关键字 | 自己编译时用 | 别人链接时也用 |
|--------|:---:|:---:|
| PRIVATE | 是 | 否 |
| INTERFACE | 否 | 是 |
| PUBLIC | 是 | 是 |

记住这张表，后面看任何 `target_*` 命令都套得上。

### 一个具体例子：fmt 是 PRIVATE，\<string> 是 INTERFACE

光定义不够，咱们落到代码上。下面这个工程有三个 target：一个极简的 `fmt`（模拟第三方格式化库）、一个对外暴露的 `mylib` 静态库、一个下游 `app` 可执行文件。`mylib` 的实现内部用 `fmt::format`，但公开头文件只用了 `std::string`。

`mylib` 的公开头文件 `include/mylib/mylib.h`：

```cpp
#pragma once
#include <string>

namespace mylib {

/// @brief 把问候语格式化成带前缀的字符串
/// @note  返回类型用 std::string —— 这是 mylib 公开 API 的一部分,
///        下游 app 也必须看到完整的 std::string 定义,
///        所以 <string> 对应的 include 路径属于 INTERFACE 需求
std::string make_greeting(const std::string& name);

}  // namespace mylib
```

`mylib` 的实现 `src/mylib.cpp`：

```cpp
#include "mylib/mylib.h"

#include "fmt.h"

namespace mylib {

std::string make_greeting(const std::string& name) {
    // fmt 是 mylib 内部实现细节,公开头文件 mylib.h 里看不到 fmt 的痕迹
    // 所以下游根本不需要知道 fmt 的存在 —— 这正是 fmt 应当为 PRIVATE 的理由
    return fmt::format("hello, {}!", name);
}

}  // namespace mylib
```

`CMakeLists.txt` 里给 `mylib` 挂属性的关键三行：

```cmake
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
target_link_libraries(mylib PRIVATE fmt)
```

`include` 写成 PUBLIC：`mylib` 自己编 `.cpp` 时要找 `mylib/mylib.h`（自己用），下游链接 `mylib` 后也要找 `mylib/mylib.h` 来包含它（别人用），两条都满足，所以是 PUBLIC。

`fmt` 写成 PRIVATE：`mylib.cpp` 内部要调 `fmt::format`（自己用），但 `mylib.h` 里没有 `fmt` 的任何符号，下游根本不需要看到 `fmt.h`（别人不用），所以是 PRIVATE。

### 改 PRIVATE 成 PUBLIC，看下游怎么被「传染」

讲概念最怕空对空。咱们直接动手把 `fmt` 从 PRIVATE 改成 PUBLIC，看下游 `app` 会发生什么。

先把工程配起来（用 Make 这个 Generator，因为它的 `flags.make` 文件能把每个 target 实际拿到的 include 路径清清楚楚列出来，Ninja 那边为了支持 C++ 模块把标志拆到别的文件里了，肉眼读不顺）：

```text
$ cmake -S . -B build -G "Unix Makefiles"
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
```

现在 `mylib` 把 `fmt` 写成 PRIVATE。看 CMake 给三个 target 各自生成的 include 标志：

```text
$ cat build/CMakeFiles/mylib.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include -I/tmp/cmake-target-demo/fmt

$ cat build/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include
```

逐字读。`mylib` 拿到两条路径：自己的 `include`（PUBLIC）加上 `fmt`（PRIVATE 自己编时也要）。`app` 只拿到一条 `include`，因为它只链接了 `mylib`，于是继承了 `mylib` 的 PUBLIC 部分（也就是 `include`），而 `fmt` 是 `mylib` 的 PRIVATE，没传过来。`app` 对 `fmt` 一无所知，这正是咱们想要的封装。

如果这时候 `app` 的 `main.cpp` 偷偷写一行 `#include "fmt.h"` 会怎样？编译器找不到这个头文件，直接挂掉。笔者实测过：

```text
$ cmake --build build --target app
[ 50%] Building CXX object CMakeFiles/app.dir/main.cpp.o
FAILED: CMakeFiles/app.dir/main.cpp.o
/tmp/cmake-target-demo/main.cpp:2:10: fatal error: fmt.h: No such file or directory
    2 | #include "fmt.h"
      |          ^~~~~~~
compilation terminated.
```

这就是 PRIVATE 的物理含义：封装是真的，不是口头说说。

现在动一行，把 `target_link_libraries(mylib PRIVATE fmt)` 改成 `target_link_libraries(mylib PUBLIC fmt)`，重新 configure，再看 `app` 的 include 标志：

```text
$ sed -i 's/target_link_libraries(mylib PRIVATE fmt)/target_link_libraries(mylib PUBLIC fmt)/' CMakeLists.txt
$ cmake -S . -B build -G "Unix Makefiles" > /dev/null
$ cat build/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include -I/tmp/cmake-target-demo/fmt
```

`app` 什么都没改，只因为上游 `mylib` 把 `fmt` 从 PRIVATE 改成 PUBLIC，`app` 就凭空多出了一条 `-I.../fmt`。现在 `app` 不用自己 `find_package(fmt)`、不用自己写 `target_link_libraries(app PRIVATE fmt)`，直接 `#include "fmt.h"` 就能编过。

这就是使用需求的**传播**：PUBLIC 把配置沿链接图向下游渗透，PRIVATE 把配置封锁在 target 内部。这种「自动传播」是现代 CMake 能把复杂依赖关系写得这么干净的根因。您只要正确标注每个依赖的公私有，下游链接一次就自动拿到该拿的全部配置。

::: warning 别用 PUBLIC 当万能补丁
看到这里您可能心动了：既然 PUBLIC 能让下游自动拿到配置，那把所有依赖都写 PUBLIC 不就省事了？千万别。PUBLIC 等于把内部实现细节泄露给下游，下游一旦依赖了您暴露出去的 `fmt` 路径，您哪天想把 `fmt` 换成 `std::format`、或者升级版本改路径，下游就跟着炸。封装是给未来留余地，PUBLIC 用得越多，重构空间越小。原则是：能 PRIVATE 就别 PUBLIC。
:::

### INTERFACE_LINK_LIBRARIES 里那个 LINK_ONLY 是什么

讲到这儿有个细节值得展开。笔者用 `get_target_property` 扒过 `mylib` 的内部属性（把 `fmt` 配成 PRIVATE 的情况下）：

```text
mylib.INCLUDE_DIRECTORIES         = /tmp/cmake-target-demo/include
mylib.INTERFACE_INCLUDE_DIRECTORIES = /tmp/cmake-target-demo/include
mylib.LINK_LIBRARIES              = fmt
mylib.INTERFACE_LINK_LIBRARIES    = $<LINK_ONLY:fmt>
```

注意最后一行。PRIVATE 不是「下游完全不知道 fmt 存在」吗，怎么 `INTERFACE_LINK_LIBRARIES` 里又出现了 `fmt`？

这里有个微妙但合理的区分：PRIVATE 封装的是 **include 路径**（下游编译时不需要 `fmt.h`），但**链接关系**是封不住的。`mylib` 是静态库，它的 `.o` 文件里引用了 `fmt::format` 的符号，链接器在最终把 `app` 链成可执行时，必须能找到 `libfmt.a` 把这些符号补上，不然链接器报 `undefined reference`。所以 CMake 用一个生成器表达式 `$<LINK_ONLY:fmt>` 表示「fmt 对下游仅参与链接、不参与编译」。这就解释了为什么您在 `app` 的 `flags.make` 里看不到 `-I.../fmt`（include 没传过来），但 `app` 还是能正常链接出可执行文件（链接关系传过来了）。PUBLIC/PRIVATE 控制的是配置的传播，不是链接图本身。

## 为什么目录级命令是反模式

理解了 target 私有性，再回头看老式 CMake 的写法，就明白它们为什么被现代 CMake 圈子一致抵制。

老式 CMake 用的是目录级、全局的命令：

```cmake
# 老式 CMake 写法,现代项目里见一次就该重构
include_directories(include)
include_directories(fmt)
add_definitions(-DUSE_FMT)
add_compile_options(-Wall)
```

`include_directories(include)` 的语义是「当前 `CMakeLists.txt` 目录及子目录里**所有** target，统统加上 `-Iinclude`」。`add_definitions(-DUSE_FMT)` 同理，所有 target 都会被定义 `-DUSE_FMT` 宏。

这种写法在小工程里看不出毛病，工程一大就崩。想象一个项目里有 `mylib`、`tests`、`benchmarks`、`tools` 四五个 target，您在顶层 `CMakeLists.txt` 写了一行 `add_compile_options(-Wall -Wextra -Werror)`，本意是给主库开严格警告，结果 `tests` 子目录下用 Catch2 写测试的那堆第三方代码也继承了 `-Werror`，编译一片红。您再去 `tests/CMakeLists.txt` 里想办法把 `-Werror` 关掉，又得记一堆绕过的写法。

再想象 `mylib` 内部用 `fmt`，您图省事在顶层写了 `include_directories(fmt)`，结果 `tools` 那个本来不该知道 `fmt` 的 target 也拿到了 `-Ifmt`，它的源码哪天不小心 `#include "fmt.h"` 也能编过，封装被悄悄打穿了。维护者回头想换掉 `fmt`，根本不知道哪些 target 是有意用 `fmt`，哪些是被全局命令顺带沾染的。

现代 CMake 用 target 级命令解决这两个问题。`target_include_directories(mylib PRIVATE fmt)` 把 `fmt` 的路径死死锁在 `mylib` 这个 target 内部，既不会泄漏到 `tools`，也不会泄漏到下游 `app`（因为 PRIVATE）。每个 target 自带一份配置边界，谁的依赖谁负责声明，依赖图清晰可追溯。

写法对照：

```cmake
# 老式(目录级,全局污染)
include_directories(include)
add_definitions(-DMYLIB_EXPORTS)

# 现代(target 级,边界清晰)
target_include_directories(mylib PUBLIC include)
target_compile_definitions(mylib PRIVATE MYLIB_EXPORTS)
```

迁移规则也直白：把所有 `include_directories()` 换成 `target_include_directories()`、`add_definitions()` 换成 `target_compile_definitions()`、`add_compile_options()` 换成 `target_compile_options()`，每个命令前面都加上具体 target 的名字。这是把老工程拉进现代 CMake 最低成本的一步。

::: details 顶层还能不能用变量设 C++ 标准
您会看到很多 `CMakeLists.txt` 顶层写 `set(CMAKE_CXX_STANDARD 17)`。这其实也是一种目录级（全局）设置，它把 `CXX_STANDARD` 属性赋给当前目录下所有 target。这种用法目前还能接受，因为 C++ 标准对绝大多数工程而言就是「全工程统一」的全局属性。但更现代、更精确的写法是 `target_compile_features(mylib PUBLIC cxx_std_17)`，把 C++ 标准也变成 target 的使用需求，下游链接 `mylib` 自动继承 C++17 要求。下一篇讲 `find_package` 时会再回来对比这两种写法。
:::

## 实战：拆一个双 target 工程

把前面讲的拼起来。咱们用一个完整可跑的工程，演示 `mylib` 静态库 + `app` 可执行的双 target 配置，看 PUBLIC/PRIVATE 在真实构建里到底怎么流。完整工程在 `code/examples/vol7/cmake-fundamentals/02-target/`，结构如下：

```text
02-target/
├── CMakeLists.txt
├── fmt/
│   ├── fmt.h          # 模拟第三方库的极简实现
│   └── fmt.cpp
├── include/
│   └── mylib/
│       └── mylib.h    # mylib 公开头文件
├── src/
│   └── mylib.cpp      # mylib 实现
└── main.cpp           # app 可执行
```

完整 `CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.20)
project(target_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# fmt:仅在本工程内部使用的极简"第三方库",真实工程会换成 find_package(fmt REQUIRED)
add_library(fmt STATIC fmt/fmt.cpp)
target_include_directories(fmt PUBLIC fmt)

# mylib:对外暴露的库,公开头文件 include/mylib/mylib.h 用了 std::string
add_library(mylib STATIC src/mylib.cpp)
target_include_directories(mylib PUBLIC include)
# fmt 在这里写成 PRIVATE —— mylib.cpp 内部要用,但 mylib.h 完全不暴露 fmt
target_link_libraries(mylib PRIVATE fmt)

# app:下游可执行,只链接 mylib,对 fmt 一无所知
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mylib)
```

读这份配置，从下往上看更清楚思路。`app` 只声明「我链接 `mylib`」，其它什么都没说。`mylib` 把自己的 `include` 目录 PUBLIC 出去，下游链接我时自动拿到这条路径；把 `fmt` 锁在 PRIVATE，下游别想知道我用 `fmt`。`fmt` 自己作为一个 STATIC 库存在，`include` 是它自己的 PUBLIC（这样 `mylib` 链接它时能拿到 `fmt.h` 路径）。

三步命令把工程跑通：

```text
$ cmake -S . -B build -G Ninja && cmake --build build && ./build/app
-- The CXX compiler identification is GNU 16.1.1
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/sbin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (0.2s)
-- Generating done (0.0s)
-- Build files have been written to: /tmp/cmake-target-demo/build
[1/6] Building CXX object CMakeFiles/fmt.dir/fmt/fmt.cpp.o
[2/6] Linking CXX static library libfmt.a
[3/6] Building CXX object CMakeFiles/mylib.dir/src/mylib.cpp.o
[4/6] Linking CXX static library libmylib.a
[5/6] Building CXX object CMakeFiles/app.dir/main.cpp.o
[6/6] Linking CXX executable app
hello, world!
```

六步的顺序里能看出依赖图。`fmt` 先编（步骤 1-2，它不依赖别人），`mylib` 后编（步骤 3-4，它依赖 `fmt`），`app` 最后编（步骤 5-6，它依赖 `mylib`）。Ninja 自动按依赖关系排好序，您什么都不用管。

最后那一行 `hello, world!` 是 `app` 跑出来的。它在 `main.cpp` 里只 `#include "mylib/mylib.h"`，编译器却能找到这个头文件，靠的就是 `mylib` 把 `include` 标成 PUBLIC，下游 `app` 链接 `mylib` 时自动继承了 `-I.../include`。

如果咱们想验证这条继承真的在起作用，最直接的办法是看 `app` 实际拿到的 include 标志。换成 Make 这个 Generator configure 一次，读 `app.dir/flags.make`：

```text
$ cmake -S . -B build-mk -G "Unix Makefiles" > /dev/null
$ cat build-mk/CMakeFiles/app.dir/flags.make | grep INCLUDES
CXX_INCLUDES = -I/tmp/cmake-target-demo/include
```

`app` 没有自己写过一行 `target_include_directories`，但它的编译命令里硬是有 `-I.../include`。这就是 PUBLIC 使用需求在背后默默干的活。`fmt` 那条路径没出现，因为 `mylib` 把 `fmt` 标成了 PRIVATE，封装得严严实实。

## 配套示例

本文的双 target 工程在仓库示例目录可直接构建：

```text
code/examples/vol7/cmake-fundamentals/02-target/
├── CMakeLists.txt
├── fmt/
│   ├── fmt.h
│   └── fmt.cpp
├── include/mylib/mylib.h
├── src/mylib.cpp
└── main.cpp
```

进入该目录后，照搬上一节那三步命令即可复现全部输出。想动手感受 PUBLIC/PRIVATE 的传播，把 `target_link_libraries(mylib PRIVATE fmt)` 改成 PUBLIC，重新 configure，再 `cat build-mk/CMakeFiles/app.dir/flags.make | grep INCLUDES`，看 `app` 凭空多出来的那条 `-I.../fmt`。

到这里，target 这个对象、`target_*` 这一族成员方法、PUBLIC/PRIVATE/INTERFACE 这三态使用需求，应该都落到了实处。下一篇要解决一个更实际的问题：真实工程里 `fmt` 不是咱们手写的，得用 `find_package(fmt)` 从系统或 vcpkg/Conan 里把第三方库接进来，`find_package` 给咱们返回的 `fmt::fmt` 这种带命名空间的目标到底是什么、它身上挂的 PUBLIC/INTERFACE 配置怎么自动流到您的工程里。同时还会回到一个悬而未决的问题：设置 C++ 标准，到底是 `set(CMAKE_CXX_STANDARD 17)` 这种目录级写法好，还是 `target_compile_features(mylib PUBLIC cxx_std_17)` 这种 target 级写法好。
