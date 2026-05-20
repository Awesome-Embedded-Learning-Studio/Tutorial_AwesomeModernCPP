---
title: Compilers, Toolchains, and Project Design Baselines
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
conference: cppcon
conference_year: 2025
talk_title: 'C++: Some Assembly Required'
speaker: Matt Godbolt
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
chapter: 2
order: 6
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/06-toolchain-and-project-design.md
  source_hash: 785fd22acb2fbd9570aed5010e48a4ecea572db0cb90cbd1072483bbf7766425
  translated_at: '2026-05-20T04:38:49.593476+00:00'
  engine: anthropic
  token_count: 3237
---
# Assembling a C++ Project: Compilers, Toolchains, and Those "Non-Standard but Excellent" Libraries

Many developers' understanding of the C++ ecosystem stops at "the language itself plus the standard library" — write code, compile, run, done. But if we trace the entire engineering workflow, the C++ language itself is just one small piece. To actually assemble a set of components into something that runs, we need far more than just C++ syntax. Today, we want to talk about this "assembly" process and the infrastructure that supports it.

## First, a mindset correction: not all great things make it into the standard

Many people hold a deep-rooted misconception that if a library is good enough and important enough, it "should" be adopted into the standard library. For example, seeing `std::optional` land in C++17<RefLink :id="2" preview="std::optional (C++17)" /> and `std::format` land in C++20<RefLink :id="3" preview="std::format (C++20)" />, they naturally assume this is the ultimate destination for all excellent libraries. But that is simply not how it works.

The standardization process has its own logic and thresholds. Some library patterns may be fundamentally unsuitable for the standard, or the maintainers may never have intended to submit them — they exist as independent, high-quality libraries, ready to be used as-is. The most typical example is Abseil<RefLink :id="4" preview="Google Abseil C++ 库" />, Google's open-source C++ library that contains many highly practical components, such as enhanced versions of `absl::StatusOr`, `absl::Span`, and `absl::string_view`. They didn't make it into the standard, nor do they need to, but their quality is excellent and they are widely used in production environments.

Another point worth noting: it's not only massive projects backed by large corporations that make it into the standard. Small consortia or even individuals can get code into the standard, provided their proposals are solid and well-argued. Of course, alliances formed by GPU vendors and large HPC institutions do have strong pushing power, which is why things like parallel computing and SIMD have advanced so quickly in the standard. But the key point is that the channel is open — it's not a game exclusively for giants.

So the right mindset should be: stop staring at the standard library waiting for "official solutions," and instead actively seek out those mature, high-quality third-party libraries. Although the C++ ecosystem lacks a centralized distribution platform like Rust's crates.io, making library discovery a bit more effortful, good things do exist.

## The real assembly begins only after the code is written

Alright, let's assume we've selected our components and finished writing the code. What next? Turning C++ code into an executable requires far more than just C++ itself.

First, we need a compiler. We are actually very fortunate right now to have three major players at our disposal: GCC, Clang, and MSVC, plus EDG<RefLink :id="5" preview="EDG 商业 C++ 前端" /> (primarily used for standard conformance testing and certain commercial scenarios). These compilers are all high quality, and some of them are open-source projects maintained by the community. You might take this for granted, but a look back at history shows just how far we've come.

The earliest C++ compiler was essentially Cfront<RefLink :id="6" preview="Cfront: 最早的 C++ 编译器" />, written by Bjarne Stroustrup — a C++-to-C translator that took C++ code, converted it to C code, and then fed that intermediate output to a regular C compiler. C++ was originally "parasitic" on C's compilation infrastructure.

Today, things are completely different. Both GCC and Clang have mature C++ frontends, with increasingly good support for each standard version. My current primary environment is GCC 16.1.1 running on Arch Linux WSL, with Clang 17 used for cross-validation, and occasionally MSVC 19.38 on Windows to ensure cross-platform compatibility. I've stepped on quite a few landmines regarding toolchain versions, which I'll cover in a separate post later.

But the compiler is only the first step. After compiling individual translation units into object files, we still need a linker to stitch them together. Many people have used C++ for years without ever giving the linker a second glance — because in most cases, a single `g++ main.cpp other.cpp` command gets the job done, and the linker works silently in the background, its presence barely felt. That is, until you hit a bizarre ODR (One Definition Rule) violation causing a link error — the same inline function expanded into different versions in two translation units, and the linker throws a completely incomprehensible symbol conflict. Only then do you realize just how complex and important the linker really is.

The core point here is this: when we complain that "C++ is hard to use," we are often not actually complaining about the C++ language itself, but about some part of this assembly process — maybe the compiler spewed a screenful of incomprehensible template errors, maybe the linker can't find a symbol, or maybe we don't know how to integrate a third-party library correctly. When we break these steps apart, each one has corresponding tools and solutions — they're just scattered everywhere, waiting to be assembled by us.

## A simple example to experience "assembly"

Here is a very small example that doesn't involve any complex logic — it simply demonstrates what the compiler and the linker each do during the process of going from "multiple source files" to "one executable."

First is the header file `math_utils.h`, which just declares a function:

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

Then there's another header file `format_utils.h`, which depends on the above `math_utils.h`:

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

Finally, `main.cpp`:

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

This example is almost absurdly simple, but it's perfect for demonstrating the step-by-step execution of the compilation process. You can manually control each step with the following commands:

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

If you use `-E` to inspect the preprocessed `main.ii` file, you'll find that the contents of `math_utils.h` and `format_utils.h` have both been expanded into it. This is why function definitions in header files need `inline` or `constexpr`<RefLink :id="7" preview="constexpr 隐式 inline" /> — otherwise, if two different `.cpp` files both include the same header, the linker will see two copies of the function definition and immediately report an ODR violation.

There's a common misconception about `inline`: many people think it's merely a "hint suggesting the compiler inline the function." But in reality, the true purpose of `inline` in C++ is to allow the same function to be defined in multiple translation units without violating the ODR<RefLink :id="8" preview="inline 关键字与 ODR 豁免" />. Whether the compiler performs inline optimization is entirely up to it, and it has no necessary connection to whether you say `inline` or not.

## Compiler selection: current practice

For day-to-day development, I primarily use GCC, supplemented by Clang. The reason is simple: GCC has the best ecosystem on Linux, and I'm familiar with its error messages. Clang's error diagnostics are indeed friendlier than GCC's in certain scenarios (especially template-related ones), so when I encounter an error I can't decipher, I switch to Clang and compile again to look at the problem from a different angle.

```bash
# 同一份代码，用两个编译器各编一次，对比报错信息
g++ -std=c++20 -Wall -Wextra main.cpp -o main_gcc
clang++ -std=c++20 -Wall -Wextra main.cpp -o main_clang
```

I strongly recommend building this habit. For the same compilation error, GCC might spit out a full screen of template instantiation backtraces, while Clang can sometimes point out the problem in a more concise way. The reverse is also true — sometimes GCC explains things more clearly. Cross-validating with two compilers saves a lot of time.

I use MSVC less often, but if a project needs to be cross-platform, occasionally compiling with MSVC on Windows is absolutely necessary. Different compilers occasionally have subtle differences in their interpretation of the standard, and discovering these early is far better than encountering problems after deployment.

---

# Editors and Build Systems: From "Good Enough to Write In" to the Pitfalls of Modules

## Editors: please help me understand this code

When it comes to editor selection, many people have indeed taken long detours. When I first started learning C++, I used VS Code with a rudimentary C/C++ extension — code completion often took forever to pop up, and error messages were always those red squiggly lines that didn't speak human. At the time, I even thought, "I guess C++ development is just like this; editors can't help you much." Later, when I saw CLion's code completion, refactoring, and real-time static analysis, it hit me — it wasn't that C++ was incapable, it was that the tool was inadequate.

But I don't want to start an "editor holy war" here. I just want to say one thing: **never mix spaces and tabs**. I once took over a project where spaces and tabs were mixed throughout the files. In the editor, the indentation looked perfectly normal, but once pushed to CI, the formatting was completely garbled, and the error locations didn't match the actual code. Ever since then, I always configure `.clang-format` in my projects, enforcing spaces uniformly and leaving no room for mixing.

Speaking of the editor ecosystem, we're actually at a very interesting stage right now. Terminal-dwelling Vim/Neovim users can achieve an experience very close to that of an IDE through clangd + LSP, with code completion, go-to-definition, and hover documentation all readily available. But personally, CLion works out of the box, its CMake integration is native-level, and creating a new project with a properly configured CMakeLists.txt means you can hit run immediately — no need to spend two days configuring your editor. Time should be spent understanding C++, not configuring editors.

However, I've recently been running into a scenario with increasing frequency where no editor can help. I'll write a piece of fairly complex logic using several lambda expressions for callback registration. At the time of writing, it feels crystal clear, but three days later when I come back to it, I have absolutely no idea what that code is doing. I even pasted the code into CLion's built-in AI assistant and asked it to explain — after reading the explanation, I was still only half-understanding. What does this tell us? It tells us that tools can help you write code and find bugs, but they can't **think** for you. Code readability ultimately depends on the design of your abstraction layers — I've stepped on this landmine far too many times.

## Build systems: I thought CMake was the hardest part, until I touched Modules

If the editor is the "experience of writing code," then the build system is the "experience of getting code to run." And in C++, how should I put it — this experience often makes you want to smash your keyboard.

I used to think CMake was already torturous enough. Things like the `target_link_libraries` parameter passing style, which of `PUBLIC` `PRIVATE` `INTERFACE` to actually use, how to troubleshoot when `find_package` can't find a package — it took me over half a year to become reasonably proficient. But no matter how hard CMake is, it's at least something where "study it a bit and you can get started," and although the documentation reads like arcane scripture, at least documentation exists.

Until I tried C++20 Modules. When I first heard about Modules, I was thrilled — finally, no more suffering from the compilation speed issues of header inclusion. Then I tried it out — first of all, CMake's support for Modules in early versions was extremely rough. You had to manually specify how `.cppm` files were compiled into module interface units and module implementation units, and the module file formats differed between compilers: GCC uses `.gcm`<RefLink :id="9" preview="GCC 模块缓存 .gcm" />, Clang uses `.pcm`<RefLink :id="10" preview="Clang 预编译模块 .pcm" />, and MSVC has yet another format. Then there's the circular dependency problem. In the traditional header era, you could use forward declarations to break circular dependencies, but in the Modules world, this approach doesn't work quite the same way. I was stuck on this for three days, only to realize that my understanding of "module partitions" was fundamentally wrong.

Here's a minimal working example I cobbled together at the time. The code itself isn't complex, but getting it working took an entire weekend:

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

You see, the code itself is actually very intuitive — `export` marks what's publicly visible, `import` replaces `#include`, and conceptually it's much cleaner than headers. But to get these few lines of code running, you need CMake 3.28 or above, sufficient compiler support for C++20 modules, and a correctly written CMakeLists.txt. I initially tried with CMake 3.25 and got a direct error saying the module couldn't be found. I was stuck for two hours before finally realizing it was a version issue.

There's another easily overlooked limitation: CMake 3.28's support for C++20 modules is restricted to the Ninja generator and Visual Studio 2022 and above<RefLink :id="12" preview="CMake 3.28 modules 支持的生成器限制" />. Using the traditional Makefile generator currently doesn't work. This is a rather hidden pitfall — once you step on it, you remember it.

And this is only the simplest case — a single module, no partitions, no dependencies on other modules. Once the project scales up and modules start importing each other, deducing the build order becomes a nightmare. After talking with several people, I found that everyone has tripped over Modules build configuration — this isn't an isolated case.

---

# Designing for Humans: The Bottom Line for Project Design

When hearing the talk's point about "designing for humans," many people's vague intuitions suddenly gained a clear framework.

I used to have a misconception that a C++ project's impressiveness was measured by how flashy its template metaprogramming was or how sophisticated its build system was. After being brainwashed by various "modern C++ best practices," I felt projects should be equipped with a full set of intricate CMake scripts. The result? I built a few such projects, felt pretty great at the time, but came back a month later to modify some code only to find I couldn't even get it to compile — because some dependency had bumped its version and changed its interface, and there was a hardcoded version number in that sophisticated script. I was stuck for ages, eventually deleted the entire build directory and started over, wasting another two hours. This was essentially doing myself a disservice.

The talk made a crucial point: if your project is troublesome to build, requires people to install four hundred global packages, and those packages are incompatible with their machines, you're keeping potential contributors out. Many people have had this experience — you want to submit a PR to fix an obvious bug in a well-known C++ library, but the README reads like arcane scripture, the dependency list spans two pages, and it requires specific versions of Boost and LLVM. After struggling all night without getting it to run, you quietly close that PR page the next day and never go back. It's not that you don't want to contribute — it's that your patience has been exhausted.

So when building a project, we should hold one hard line: a person who knows nothing about the project should be able to go from `git clone` to running their first hello world in under five minutes. I tested this idea with a small tool I've been writing recently, and the results were remarkably good.

First, the directory structure — deliberately kept very flat:

```text
my_tool/
├── CMakeLists.txt
├── src/
│   └── main.cpp
├── include/
│   └── my_tool.hpp
└── README.md
```

No submodules, no complex directory nesting. The CMakeLists.txt is also written as straightforwardly as possible:

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

The README.md was also rewritten, dropping the usual "feature list + a bunch of badges" style in favor of directly telling people how to get it running:

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

Note that final "pitfall notes" section — I added this after stepping on those landmines myself. I used to think writing such things was "unprofessional," but now I think this is the most professional part. Because you're saving time for the next person who comes along, and saving time is the greatest kindness.

I tested this project with two colleagues — one primarily writes Python, the other primarily writes Java — and both got it running within three minutes. The Python colleague even said, "This is easier to set up than a lot of Python projects." Getting a C++ project praised for "simple configuration" — that would have been unthinkable before.

The talk also made a particularly forward-looking point: if you make your project easy to drop into and out of, you're not just helping humans — you're also helping AI agents. I've definitely experienced this recently. When using Cursor to assist with coding, I've found that if a project has a clean structure, few dependencies, and a simple build, the AI can understand more project context and give more reliable suggestions. Conversely, if the project is full of nested custom compiler flags and implicit macro definitions, the AI frequently gives suggestions that "look right but don't actually run," because it fundamentally doesn't understand what's happening in that complex build environment.

Seeing template errors gives humans a headache — it gives AI a headache too. When it sees a two-hundred-line template instantiation error stack, its responses are often generic and vague. But if the project itself is clean and highly modular, error messages will be much shorter, and both AI and humans will locate problems much faster. So "designing for humans" and "designing for AI" are actually unified on this point: both are about reducing cognitive load.

Looking back, the principle is simple. We write code that is ultimately meant to be read and used by people. The compiler only cares whether the syntax is correct, but people care about "can I quickly understand what this project does, and can I quickly make my changes and move on." Making complex things simple — that's the real skill.

It finally clicked — in the process of assembling a C++ program, the tools, the libraries, and the build systems are all just parts, but the person actually holding those parts and putting them together is the most important factor. Ignore that, and even the most precision-engineered parts are just a pile of scrap metal.

<ReferenceCard title="References">
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
