---
title: "编译器、工具链与项目设计底线"
description: "CppCon 2025 演讲笔记 —— C++: Some Assembly Required by Matt Godbolt"
conference: cppcon
conference_year: 2025
talk_title: "C++: Some Assembly Required"
speaker: "Matt Godbolt"
video_bilibili: "https://www.bilibili.com/video/BV1ptCCBKEwW?p=2"
video_youtube: "https://www.youtube.com/watch?v=zoYT7R94S3c"
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
chapter: 2
order: 6
---

# C++ 的拼装工程：编译器、工具链和那些"不进标准但很好用"的库

很多程序员对 C++ 生态的理解停留在"语言本身加上标准库"的层面——写代码、编译、运行，完事了。但梳理一下整个工程流程就会发现，C++ 这个语言本身只是整个工程里的一小块。真正要把一套组件拼装成一个能跑的东西，需要的东西远比 C++ 语法多得多。今天想聊的就是这个"拼装"过程，以及支撑它的那些基础设施。

## 先说一个认知上的修正：不是所有好东西都会进标准

很多人有个根深蒂固的误解，觉得一个库如果足够好、足够重要，它"应该"被纳入标准库。比如看到 `std::optional` 进了 C++17<RefLink :id="2" preview="std::optional (C++17)" />，`std::format` 进了 C++20<RefLink :id="3" preview="std::format (C++20)" />，就理所当然地觉得这是所有优秀库的归宿。但实际上根本不是这么回事。

标准化的过程有它自己的逻辑和门槛，有些库的模式可能压根就不适合放进标准，或者维护者从来没打算送进去——它们就是作为独立的、高质量的库存在的，直接拿来用就好。最典型的例子就是 Abseil<RefLink :id="4" preview="Google Abseil C++ 库" />，Google 开源的这套 C++ 库里有很多非常实用的组件，像 `absl::StatusOr`、`absl::Span`、`absl::string_view` 的增强版本等等。它们没有进标准，也不需要进标准，但质量非常高，很多生产环境都在用。

还有一点值得注意：并不是只有那种庞大的、背后有大公司撑腰的项目才能进标准。小型联盟甚至个人，只要提案质量够硬、论证充分，一样可以把代码送进标准。当然，像 GPU 厂商和大型 HPC 机构组成的联盟，他们对标准的推动力确实很强，所以标准里关于并行计算、SIMD 之类的东西推进得特别快。但关键是，通道是开放的，不是只有巨头才能玩。

所以正确的心态应该是：不再盯着标准库等"官方解决方案"，而是主动去寻找那些已经成熟的高质量第三方库。C++ 的生态虽然不如 Rust 那样有 crates.io 做中心化分发，找库确实费劲一些，但好东西是有的。

## 真正的拼装从写完代码之后才开始

好，假设已经选好了组件，代码也写完了。接下来呢？"把 C++ 代码变成可执行文件"这件事，需要的远不止 C++ 本身。

首先需要编译器。我们现在其实非常幸运，手头有 GCC、Clang、MSVC 这三大主力，还有 EDG<RefLink :id="5" preview="EDG 商业 C++ 前端" />（主要用于标准合规性测试和某些商业场景）。这些编译器质量都很高，而且其中一些是社区维护的开源项目。你可能觉得这是理所当然的，但回头看看历史就知道我们走了多远。

最早的 C++ 编译器本质上就是 Bjarne Stroustrup 写的 Cfront<RefLink :id="6" preview="Cfront: 最早的 C++ 编译器" />——一个 C++ 到 C 的转换器，它接收 C++ 代码，然后把它转成 C 代码，再拿普通的 C 编译器去编译那个中间产物。C++ 最初是"寄生"在 C 的编译基础设施上的。

现在当然完全不一样了。GCC 和 Clang 都有成熟的 C++ 前端，对各个标准版本的支持也越来越好。目前的主力环境是 Arch Linux WSL 上跑 GCC 16.1.1，Clang 17 用作交叉验证，偶尔在 Windows 上用 MSVC 19.38 跑一下确保跨平台没问题。工具链版本这块踩过不少坑，后面单独写一篇。

但编译器只是第一步。把一个个翻译单元编译成目标文件之后，还需要链接器把它们拼到一起。链接器这个东西，很多人用了好几年 C++ 都没正眼看过它——因为大多数情况下 `g++ main.cpp other.cpp` 一行命令就搞定了，链接器在背后默默工作，感觉不到它的存在。直到遇到一个诡异的 ODR（One Definition Rule）违规导致的链接错误——同一个内联函数在两个翻译单元里展开了不同的版本，链接器报了一个完全看不懂的符号冲突——这时候才会意识到链接器是个多么复杂且重要的东西。

核心观点是：当抱怨"C++ 难用"的时候，很多时候抱怨的其实不是 C++ 语言本身，而是这个拼装过程中的某个环节——可能是编译器报了一堆看不懂的模板错误，可能是链接器符号找不到，可能是不知道怎么把第三方库正确地集成进来。把这些环节拆开看，每一个都有对应的工具和解决方案，只是它们散落在各处，需要自己去组装。

## 用一个简单例子感受一下"拼装"

下面是一个特别小的例子，不涉及任何复杂逻辑，就是展示一下从"多个源文件"到"一个可执行文件"这个过程里，编译器和链接器各自在干什么。

首先是头文件 `math_utils.h`，就声明一个函数：

```cpp
// math_utils.h
// constexpr 函数隐式 inline（[dcl.constexpr]/1），因此可以放在头文件中
// 而不会违反 ODR——编译器也可能在编译期直接求值
constexpr int square(int x) {
    return x * x;
}

// 这个函数有定义，放在头文件里，inline 防止 ODR 违规
inline int add_one(int x) {
    return x + 1;
}
```

然后是另一个头文件 `format_utils.h`，它依赖上面的 `math_utils.h`：

```cpp
// format_utils.h
#include "math_utils.h"
#include <string>

// 把计算结果格式化成字符串
// 这里故意不用 std::format（C++20），用 to_string 保持简单
inline std::string describe(int x) {
    return "value=" + std::to_string(add_one(square(x)));
}
```

最后是 `main.cpp`：

```cpp
// main.cpp
#include "format_utils.h"
#include <iostream>

int main() {
    int input = 5;
    std::cout << describe(input) << std::endl;
    return 0;
}
```

这个例子简单到有点傻，但正好用来演示编译过程的分步执行。你可以用以下命令手动控制每一步：

```bash
# 第一步：只预处理，看编译器看到了什么
g++ -E main.cpp -o main.ii

# 第二步：只编译不链接，生成目标文件
g++ -c main.cpp -o main.o

# 第三步：链接（这个例子只有一个 .o，所以链接很简单）
g++ main.o -o main

# 运行
./main
# 输出：value=26
```

如果你用 `-E` 看一下预处理后的 `main.ii` 文件，会发现 `math_utils.h` 和 `format_utils.h` 的内容都被展开进去了。这就是为什么头文件里的函数定义需要 `inline` 或者 `constexpr`<RefLink :id="7" preview="constexpr 隐式 inline" />——否则如果两个不同的 `.cpp` 文件都 include 了同一个头文件，链接器会看到两份函数定义，直接报 ODR 违规。

关于 `inline` 有一个常见的误解：很多人以为它只是"建议编译器内联"的提示词。但实际上，`inline` 在 C++ 里真正的作用是允许同一个函数在多个翻译单元中有定义而不违反 ODR<RefLink :id="8" preview="inline 关键字与 ODR 豁免" />。内联优化编译器想怎么做就怎么做，跟你说不说 `inline` 没有必然关系。

## 编译器选型：当前实践

日常开发基本是 GCC 为主、Clang 为辅。原因很简单：GCC 在 Linux 上生态最好，报错信息比较熟悉；Clang 的错误提示在某些场景下（尤其是模板相关）确实比 GCC 更友好，所以遇到看不懂的报错时会切到 Clang 再编一次，换个角度看问题。

```bash
# 同一份代码，用两个编译器各编一次，对比报错信息
g++ -std=c++20 -Wall -Wextra main.cpp -o main_gcc
clang++ -std=c++20 -Wall -Wextra main.cpp -o main_clang
```

强烈建议养成这个习惯。同一个编译错误，GCC 可能吐出一屏幕的模板实例化回溯，而 Clang 有时候能用更简洁的方式指出问题所在。反过来也一样，有些情况 GCC 说得更清楚。两个编译器交叉验证，能省下很多时间。

MSVC 用得少，但如果项目需要跨平台，偶尔在 Windows 上用 MSVC 编一次是非常有必要的。不同编译器对标准的解读偶尔会有细微差异，早点发现比上线后出问题好。

---

# 编辑器与构建系统：从"能写就行"到 Modules 的坑

## 编辑器：求求你帮我理解这段代码

说到编辑器选择，很多人确实走了很长的弯路。刚开始学 C++ 的时候用的是 VS Code，配了个简陋的 C/C++ 插件，代码补全经常半天才弹出来，报错信息永远是那个红色的波浪线但不说人话。那时候甚至觉得"C++ 开发就是这样吧，编辑器帮不了你太多"。后来看到 CLion 的代码补全、重构、实时的静态分析，才明白——原来不是 C++ 不行，是工具不行。

但不想在这里搞"编辑器圣战"。只想说一件事：**千万别混用空格和制表符**。之前接手过一个项目，文件里空格和制表符混排，在编辑器里看着缩进完全正常，一推到 CI 上格式全乱了，报错的位置跟实际代码对不上。从那以后项目里一定配 `.clang-format`，统一用空格，不给人任何混用的机会。

说到编辑器生态，现在我们其实处在一个很有意思的阶段。终端里的 Vim/Neovim 党可以通过 clangd + LSP 把体验做到非常接近 IDE 的水平，代码补全、跳转定义、悬浮文档一应俱全。但就个人选择而言，CLion 开箱即用，CMake 集成是原生级别的，新建一个项目、配好 CMakeLists.txt，点一下运行就能跑，不需要花两天时间配编辑器。时间应该花在理解 C++ 上，不是花在配编辑器上。

不过最近越来越频繁地遇到一个场景，是任何编辑器都帮不了忙的。就是写了一段逻辑比较复杂的代码，用了好几个 lambda 做回调注册，当时写的时候觉得清晰得很，三天后回去看，完全不知道那段代码在干什么。甚至把代码贴给 CLion 内置的 AI 助手看，让它解释，看完解释之后还是半懂不懂。这说明什么？说明工具能帮你写代码、能帮你找 bug，但它没法帮你**思考**。代码的可读性最终还是要靠对抽象层次的设计来保证，这个坑踩过太多次了。

## 构建系统：以为 CMake 就是最难的了，直到碰了 Modules

如果说编辑器是"写代码的体验"，那构建系统就是"让代码跑起来的体验"，而这个体验在 C++ 里怎么说呢，经常让人想砸键盘。

之前一直觉得 CMake 已经够折磨人了。什么 `target_link_libraries` 的传参方式，`PUBLIC` `PRIVATE` `INTERFACE` 到底该用哪个，`find_package` 找不到包的时候该怎么排查，这些花了大半年才算是比较熟练了。但 CMake 再怎么难，它好歹是个"学一学就能上手"的东西，文档虽然写得像天书但至少有文档。

直到尝试 C++20 Modules。第一次听说 Modules 的时候很兴奋，心想终于不用再忍受头文件包含的编译速度问题了。然后动手试了一下——首先，CMake 对 Modules 的支持在早期版本里非常粗糙，需要手动指定 `.cppm` 文件怎么编译成模块接口单元、怎么编译成模块实现单元，不同编译器之间的模块文件格式还不一样，GCC 用 `.gcm`<RefLink :id="9" preview="GCC 模块缓存 .gcm" />，Clang 用 `.pcm`<RefLink :id="10" preview="Clang 预编译模块 .pcm" />，MSVC 又是另一套。然后还会遇到循环依赖的问题，传统头文件时代可以用前置声明来打破循环依赖，但 Modules 的世界里这个做法不完全一样，这个坑卡了三天，最后发现是对"模块分区"的理解根本就是错的。

下面是一个当时折腾出来的最小可运行例子，例子本身不复杂，但搞通它花了一整个周末：

```cpp
// math_utils.cppm (模块接口单元)
module;
#include <cmath>  // module 声明之前的 #include 是全局模块片段<RefLink :id="11" preview="C++20 全局模块片段" />，这里放传统头文件
export module math_utils;  // 声明模块名

export double compute_sqrt(double x) {
    return std::sqrt(x);
}

export namespace stats {
    double mean(const double* data, size_t count) {
        double sum = 0.0;
        for (size_t i = 0; i < count; ++i) {
            sum += data[i];
        }
        return sum / count;
    }
}
```

```cpp
// main.cpp (消费者)
import math_utils;  // 不是 #include，是 import
#include <iostream>

int main() {
    std::cout << "sqrt(16) = " << compute_sqrt(16.0) << "\n";
    double data[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::cout << "mean = " << stats::mean(data, 5) << "\n";
    return 0;
}
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(module_test CXX)

# 必须显式开启，而且不同编译器行为有差异
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(module_test main.cpp math_utils.cppm)
target_compile_features(module_test PRIVATE cxx_std_20)
```

你看，代码本身其实很直观，`export` 标记什么对外可见，`import` 替代 `#include`，概念上比头文件清爽多了。但为了让这几行代码跑起来，需要 CMake 3.28 以上、编译器对 C++20 modules 有足够支持、而且 CMakeLists.txt 里的配置还不能写错。一开始用 CMake 3.25 试，直接报错说找不到模块，卡了两个小时，最后才意识到是版本问题。

还有一个容易忽略的限制：CMake 3.28 对 C++20 模块的支持仅限 Ninja 生成器和 Visual Studio 2022 及以上版本<RefLink :id="12" preview="CMake 3.28 modules 支持的生成器限制" />，使用传统的 Makefile 生成器目前是不行的。这算是个比较隐蔽的坑，踩过一次就记住了。

而且这还只是最简单的情况——单个模块、没有分区、没有依赖其他模块。一旦项目规模上来，模块之间互相 import，构建顺序的推导就变成了一场噩梦。跟好几个人聊过之后发现大家都在 Modules 的构建配置上栽过跟头，这不是个例。

---

# 为人类考虑：项目设计的底线

听到演讲里关于"为人类考虑"的说法时，很多人模糊的直觉突然就有了清晰的框架。

之前一直有个误区，觉得一个 C++ 项目牛不牛，看的是它的模板元编程用得多花哨、构建系统多精密。被各种"现代 C++ 最佳实践"洗脑之后，觉得项目就该配上一整套精密的 CMake 脚本。结果呢？自己搭过几个这样的项目，当时觉得挺爽，过了一个月回来改代码，发现连编译都跑不起来了——因为某个依赖升了个版本，接口变了，而那套精密的脚本里有个硬编码的版本号。卡了半天，最后把整个构建目录删了重来，又折腾了两小时。这其实是在给自己帮倒忙。

演讲里提到一个很关键的点：如果你的项目构建起来很麻烦，要求别人安装四百个全局包，然后这些包还跟电脑不兼容，那你就是在把潜在贡献者挡在门外。这种体会很多人都有——想给一个挺有名的 C++ 库提个 PR，修复一个很明显的问题，结果 README 写得像天书，依赖列表列了两页，还要求特定版本的 Boost 和特定版本的 LLVM。折腾了一晚上没跑通，第二天默默关掉了那个 PR 页面，再也没回去过。不是不想贡献，是耐心被消耗光了。

所以建项目的时候，应该死守一条底线：一个完全不认识这个项目的人，从 git clone 到跑出第一个 hello world，不应该超过五分钟。拿最近在写的一个小工具验证了一下这个想法，效果出奇地好。

先看目录结构，刻意保持得非常扁平：

```text
my_tool/
├── CMakeLists.txt
├── src/
│   └── main.cpp
├── include/
│   └── my_tool.hpp
└── README.md
```

没有子模块，没有复杂的目录嵌套。CMakeLists.txt 也写得尽量直白：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_tool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 核心就这几行：找依赖、加可执行文件、链接
find_package(fmt REQUIRED)

add_executable(my_tool src/main.cpp)
target_include_directories(my_tool PRIVATE include)
target_link_libraries(my_tool PRIVATE fmt::fmt)
```

README.md 也重新写了，不再是那种"功能列表 + 一堆 badge"的风格，而是直接告诉怎么跑起来：

```markdown
# my_tool

一个做 XXX 的小工具。

## 构建

前提：你需要一个支持 C++20 的编译器，以及 fmt 库。

Ubuntu/Debian:
    sudo apt install libfmt-dev g++

macOS:
    brew install fmt

然后：
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)

构建产物在 build/my_tool。

## 踩坑记录

- 如果你用的是 GCC 11 以下，可能遇到 XXX 问题，升级到 GCC 12 即可
- fmt 版本需要 >= 9.0，太旧的话会报 XXX 错误
```

注意最后那个"踩坑记录"部分——这是自己踩过坑之后加的。之前觉得写这种东西"不专业"，现在觉得这才是最专业的部分。因为你在帮下一个来的人省时间，而省时间就是最大的善意。

拿这个项目问了两个同事，一个主要写 Python，一个主要写 Java，都在三分钟内跑通了。那个写 Python 的同事甚至说"这比很多 Python 项目的环境配置还简单"。C++ 项目能被夸"配置简单"，这在以前是不敢想的。

演讲里还提到一个特别有前瞻性的点：如果你把项目做得容易切入和跳出，你不仅在帮人类，也在帮 AI agents。这个最近确实有体会。用 Cursor 辅助写代码的时候发现，如果一个项目结构清晰、依赖少、构建简单，AI 能理解的项目上下文就多，给出的建议就靠谱。反过来，如果项目里有一堆嵌套的自定义编译器标志、隐式的宏定义，AI 就经常给出"看起来对但实际跑不通"的建议，因为它根本没理解那个复杂的构建环境里到底发生了什么。

模板报错看到就头疼，AI 也头疼——它看到那种长达两百行的模板实例化错误栈，给出的回复经常是泛泛而谈。但如果项目本身干净、模块化程度高，错误信息就会短很多，AI（以及人类）定位问题的速度就会快很多。所以"为人类考虑"和"为 AI 考虑"在这点上其实是统一的：都是降低认知负担。

回头看看，道理很简单。我们写代码，最终是给人看的、给人用的。编译器只关心语法对不对，但人关心的是"我能不能快速理解这个项目在干什么、我能不能快速改完走人"。把复杂的东西做得简单才是真本事。

到这里终于搞通了——组装 C++ 程序的过程中，那些工具、那些库、那些构建系统都是零件，但真正拿着这些零件去拼的人，才是最重要的。忽略了这个，再精密的零件也只是一堆废铁。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Kitware"
    title="CMake: Cross-Platform Build System"
    publisher="Kitware Inc."
    :year="2000"
    chapter="de facto standard C++ build system; FetchContent, find_package"
    url="https://cmake.org/"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::optional"
    publisher="cppreference.com"
    :year="2017"
    chapter="C++17 标准库可选值包装器"
    url="https://en.cppreference.com/cpp/utility/optional"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Formatting library (std::format)"
    publisher="cppreference.com"
    :year="2020"
    chapter="C++20 格式化库，基于 Python 风格的格式字符串"
    url="https://en.cppreference.com/cpp/utility/format"
  />
  <ReferenceItem
    :id="4"
    author="Google"
    title="Abseil C++ Common Libraries"
    publisher="Google LLC"
    :year="2017"
    chapter="Google 开源的 C++ 基础库，包含 absl::StatusOr、absl::Span、absl::string_view 等"
    url="https://abseil.io/"
  />
  <ReferenceItem
    :id="5"
    author="Edison Design Group"
    title="EDG C++ Front End"
    publisher="Edison Design Group"
    :year="1994"
    chapter="商业 C/C++ 语言前端，广泛用于编译器和静态分析工具"
    url="https://www.edg.com/"
  />
  <ReferenceItem
    :id="6"
    author="Bjarne Stroustrup"
    title="Cfront — The Original C++ Compiler"
    publisher="AT&T Bell Labs"
    :year="1983"
    chapter="最早的 C++ 编译器，将 C++ 源码翻译为 C 代码后再由 C 编译器编译"
    url="https://en.wikipedia.org/wiki/Cfront"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="constexpr specifier (since C++11)"
    publisher="cppreference.com"
    :year="2011"
    chapter="constexpr 函数隐式 inline，允许定义在头文件中而不违反 ODR"
    url="https://en.cppreference.com/cpp/language/constexpr"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="inline specifier"
    publisher="cppreference.com"
    :year="2011"
    chapter="inline 的核心语义是允许同一函数在多个翻译单元中定义而不违反 ODR"
    url="https://en.cppreference.com/cpp/language/inline"
  />
  <ReferenceItem
    :id="9"
    author="Free Software Foundation"
    title="C++ Module Mapper (GCC)"
    publisher="GNU Project"
    :year="2021"
    chapter="GCC 模块缓存使用 .gcm 格式，存储在 gcm.cache 目录中"
    url="https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Module-Mapper.html"
  />
  <ReferenceItem
    :id="10"
    author="LLVM Project"
    title="Standard C++ Modules — Clang Documentation"
    publisher="LLVM Foundation"
    :year="2021"
    chapter="Clang 使用 .pcm (Precompiled Module) 格式存储模块编译产物"
    url="https://clang.llvm.org/docs/StandardCPlusPlusModules.html"
  />
  <ReferenceItem
    :id="11"
    author="cppreference.com"
    title="Modules (since C++20)"
    publisher="cppreference.com"
    :year="2020"
    chapter="C++20 模块系统：module 声明、全局模块片段、export、import 语法"
    url="https://en.cppreference.com/cpp/language/modules"
  />
  <ReferenceItem
    :id="12"
    author="Kitware"
    title="CMake 3.28 Release Notes"
    publisher="Kitware Inc."
    :year="2023"
    chapter="C++20 named modules 支持，仅限 Ninja 和 Visual Studio (VS 2022+) 生成器"
    url="https://cmake.org/cmake/help/latest/release/3.28.html"
  />
</ReferenceCard>
