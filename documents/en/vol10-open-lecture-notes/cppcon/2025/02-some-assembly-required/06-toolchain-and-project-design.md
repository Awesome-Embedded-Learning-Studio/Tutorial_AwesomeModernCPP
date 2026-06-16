---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 20
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: Compilers, Toolchains, and Project Design Baselines
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/06-toolchain-and-project-design.md
  source_hash: 024375a6cb5811922a8152c34647724acafccf586059ebd7f7a1ad727db4f913
  translated_at: '2026-06-16T03:51:53.453347+00:00'
  engine: anthropic
  token_count: 3271
---
# The C++ Assembly Project: Compilers, Toolchains, and "Non-Standard but Excellent" Libraries

Many programmers' understanding of the C++ ecosystem stops at "the language plus the standard library"—write code, compile, run, done. But if we map out the entire engineering workflow, we realize that the C++ language itself is just one small piece of the whole puzzle. To actually assemble a set of components into something that runs, we need much more than just C++ syntax. Today, I want to discuss this "assembly" process and the infrastructure that supports it.

## First, a Correction: Not All Good Things Enter the Standard

Many people have a deep-seated misconception that if a library is good enough and important enough, it "should" be included in the standard library. For example, seeing `std::optional` enter C++17<RefLink :id="2" preview="std::optional (C++17)" /> and `std::format` enter C++20<RefLink :id="3" preview="std::format (C++20)" />, they naturally assume this is the destiny of all excellent libraries. But in reality, that's not how it works at all.

The standardization process has its own logic and thresholds. Some library patterns might simply be unsuitable for the standard, or the maintainers never intended to send them there—they exist as independent, high-quality libraries that are ready to use. The most typical example is Abseil<RefLink :id="4" preview="Google Abseil C++ Library" />. This open-source C++ library from Google contains many very practical components, like enhanced versions of `optional`, `span`, and `string_view`. They haven't entered the standard, nor do they need to, but their quality is extremely high, and they are used in many production environments.

Another point worth noting: it's not only massive projects backed by big companies that can enter the standard. Small alliances or even individuals, as long as their proposal quality is solid and the argument is sufficient, can also get code into the standard. Of course, alliances formed by GPU vendors and large HPC institutions do have strong push for the standard, so things like parallel computing and SIMD have advanced particularly quickly. But the key is that the channel is open; it's not a game only for giants.

So the correct mindset should be: stop staring at the standard library waiting for "official solutions," and instead actively seek out those mature, high-quality third-party libraries. Although the C++ ecosystem lacks a centralized distribution system like Rust's crates.io (making finding libraries a bit harder), the good stuff is out there.

## The Real Assembly Starts After You Finish Writing Code

Okay, let's assume we've selected our components and written the code. What's next? Turning "C++ code into an executable file" requires much more than just C++ itself.

First, we need a compiler. We are actually quite lucky to have three major players: GCC, Clang, and MSVC, plus EDG<RefLink :id="5" preview="EDG commercial C++ front end" /> (mainly used for standard compliance testing and certain commercial scenarios). These compilers are high quality, and some are open-source projects maintained by the community. You might take this for granted, but looking back at history shows how far we've come.

The earliest C++ compilers were essentially Cfront<RefLink :id="6" preview="Cfront: The original C++ compiler" /> written by Bjarne Stroustrup—a C++ to C translator. It took C++ code, converted it into C code, and then used a regular C compiler to compile that intermediate product. C++ was initially "parasitic" on C's compilation infrastructure.

Now, of course, it's completely different. GCC and Clang both have mature C++ front ends, and support for various standard versions is getting better. My current main environment is GCC 16.1.1 on Arch Linux WSL, with Clang 17 for cross-validation, and occasionally MSVC 19.38 on Windows to ensure cross-platform compatibility. I've stepped on plenty of potholes regarding toolchain versions, which I'll write about separately in another post.

But the compiler is just the first step. After compiling individual translation units into object files, we need a linker to stitch them together. Many people have used C++ for years without giving the linker a second thought—because in most cases, a single `g++` command handles it. The linker works silently in the background, unnoticed. That is, until you encounter a bizarre ODR (One Definition Rule) violation causing a link error—where the same inline function is expanded into different versions in two translation units, and the linker reports a completely incomprehensible symbol conflict. Only then do you realize how complex and important the linker really is.

The core point is: when complaining that "C++ is hard to use," we are often not complaining about the C++ language itself, but about a specific link in this assembly process—it could be the compiler spitting out a screen full of unintelligible template errors, the linker not finding symbols, or not knowing how to integrate third-party libraries correctly. If we break these links down, each has corresponding tools and solutions. They are just scattered around and need to be assembled manually.

## A Simple Example to Experience "Assembly"

Here is a very small example. It doesn't involve any complex logic; it just demonstrates what the compiler and linker do respectively in the process of going from "multiple source files" to "one executable file."

First is the header file `add.h`, just declaring a function:

```cpp
// add.h
#pragma once

int add(int a, int b);
```

Then is another header file `utils.h`, which depends on the `add` above:

```cpp
// utils.h
#pragma once

#include "add.h"

inline int add_one(int x) {
    return add(x, 1);
}
```

Finally, `main.cpp`:

```cpp
// main.cpp
#include "utils.h"
#include <iostream>

int main() {
    std::cout << add_one(10) << std::endl;
    return 0;
}
```

This example is so simple it's silly, but it's perfect for demonstrating the step-by-step execution of the compilation process. You can manually control each step with the following commands:

```bash
# Preprocess only (.ii file)
g++ -E main.cpp -o main.ii

# Compile to assembly
g++ -S main.cpp -o main.s

# Compile to object file
g++ -c main.cpp -o main.o

# Link object files to executable
g++ main.o add.o -o my_app
```

If you use `g++ -E` to look at the preprocessed `main.ii` file, you'll see the contents of `iostream` and `utils.h` have been expanded into it. This is why function definitions in header files need `inline` or `constexpr`<RefLink :id="7" preview="constexpr implicitly inline" />—otherwise, if two different `.cpp` files include the same header, the linker will see two copies of the function definition and immediately report an ODR violation.

There is a common misconception about `inline`: many think it's just a hint to "suggest the compiler inline." But actually, `inline`'s true role in C++ is to allow the same function to be defined in multiple translation units without violating the ODR<RefLink :id="8" preview="inline keyword and ODR exemption" />. Whether the compiler performs the inlining optimization is up to it; it has no necessary connection to whether you say `inline` or not.

## Compiler Selection: Current Practice

Daily development is primarily GCC, supplemented by Clang. The reason is simple: GCC has the best ecosystem on Linux, and I'm familiar with its error messages. Clang's error hints are indeed friendlier than GCC in some scenarios (especially templates), so when I encounter an error I don't understand, I switch to Clang to compile again and get a different perspective.

```bash
# Build with GCC
g++ main.cpp -o app_gcc -Wall -Wextra -std=c++20

# Build with Clang
clang++ main.cpp -o app_clang -Wall -Wextra -std=c++20
```

I strongly recommend developing this habit. For the same compilation error, GCC might spit out a screen full of template instantiation backtraces, while Clang can sometimes point out the problem in a more concise way. The reverse is also true; sometimes GCC is clearer. Cross-validating with two compilers saves a lot of time.

I use MSVC less, but if a project needs to be cross-platform, compiling with MSVC on Windows occasionally is very necessary. Different compilers occasionally have subtle differences in interpreting the standard; discovering them early is better than having problems after launch.

---

# Editors and Build Systems: From "Just Works" to the Pitfalls of Modules

## Editors: Please Help Me Understand This Code

Regarding editor selection, many people have indeed taken a long detour. When I started learning C++, I used VS Code with a rudimentary C/C++ plugin. Code completion took forever to pop up, and error messages were always red squigglies that didn't speak human. I even thought "C++ development is just like this; editors can't help you much." Later, seeing CLion's code completion, refactoring, and real-time static analysis, I realized—it's not that C++ is bad, it's that the tools were bad.

But I don't want to start an "editor war" here. I just want to say one thing: **Never mix spaces and tabs**. I once took over a project where spaces and tabs were mixed. The indentation looked completely normal in the editor, but once pushed to CI, the format was completely messed up, and the error locations didn't match the actual code. Since then, I always configure `.editorconfig` in projects to unify spaces, leaving no room for mixing.

Speaking of the editor ecosystem, we are actually at a very interesting stage now. Terminal Vim/Neovim users can achieve an experience very close to an IDE via clangd + LSP, with code completion, go-to-definition, and hover docs all available. But personally, CLion works out of the box. Its CMake integration is native-level. Create a new project, configure `CMakeLists.txt`, click run, and it goes. No need to spend two days configuring the editor. Time should be spent understanding C++, not configuring the editor.

However, recently, I've encountered a scenario more and more frequently where no editor can help. I write a piece of complex logic using several lambdas for callback registration. It feels very clear when writing it, but three days later, I look at it and have no idea what that code is doing. I even pasted the code to CLion's built-in AI assistant and asked it to explain. After reading the explanation, I still only half-understood. What does this show? It shows that tools can help you write code and find bugs, but they can't help you **think**. Code readability ultimately relies on the design of abstraction layers. I've stepped in this pit too many times.

## Build Systems: Thought CMake Was Hard, Until I Met Modules

If the editor is the "writing experience," then the build system is the "getting it running experience." And in C++, this experience often makes you want to smash your keyboard.

I used to think CMake was torture enough. What kind of argument passing `target_link_libraries` uses, whether to use `target_include_directories`, `include_directories`, or `link_directories`, how to troubleshoot when `find_package` can't find a package—it took over a year to get proficient. But as hard as CMake is, it's at least something you can "learn and pick up," and although the documentation reads like a heavenly book, at least it exists.

Until I tried C++20 Modules. When I first heard about Modules, I was excited, thinking finally no more suffering from header inclusion compilation speeds. Then I tried it—first, CMake's support for Modules in early versions was very rough. You had to manually specify how `.cpp` files compile into module interface units vs. module implementation units. Module file formats differed between compilers: GCC uses `.gcm`<RefLink :id="9" preview="GCC module cache .gcm" />, Clang uses `.pcm`<RefLink :id="10" preview="Clang precompiled module .pcm" />, and MSVC uses another set. Then you hit circular dependency issues. In the traditional header era, you could use forward declarations to break circular dependencies, but in the world of Modules, this approach isn't quite the same. I was stuck on this for three days, finally realizing my understanding of "module partitions" was simply wrong.

Here is a minimal runnable example I figured out at the time. The code itself isn't complex, but getting it working took a whole weekend:

```cpp
// math.ixx (module interface)
export module math;

export int add(int a, int b) {
    return a + b;
}
```

```cpp
// main.cpp
import math;
import <iostream>;

int main() {
    std::cout << add(10, 20) << std::endl;
    return 0;
}
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(ModulesDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(main
    main.cpp
    math.ixx
)

# Explicitly enable C++ modules support
set_target_properties(main PROPERTIES
    CXX_STANDARD 20
    CXX_EXTENSIONS OFF
)
```

You see, the code itself is very intuitive. `export` marks what is visible, `import` replaces `include`. Conceptually, it's much cleaner than headers. But to get these few lines running, you need CMake 3.28 or higher, a compiler with sufficient C++20 modules support, and the `CMakeLists.txt` configuration must be correct. I initially tried with CMake 3.25 and it directly errored saying it couldn't find the module. I was stuck for two hours before realizing it was a version issue.

There's another easily overlooked limitation: CMake 3.28's support for C++20 modules is limited to the Ninja generator and Visual Studio 2022 and above<RefLink :id="12" preview="CMake 3.28 modules support generator limitations" />. Using the traditional Makefile generator currently doesn't work. This is a fairly hidden pit; once you step in it, you remember.

And this is just the simplest case—single module, no partitions, no dependencies on other modules. Once the project scales up and modules import each other, deriving the build order becomes a nightmare. After talking to several people, I found everyone has tripped over Modules build configuration. This isn't an isolated case.

---

# Design for Humans: The Bottom Line of Project Design

When hearing the talk about "design for humans," many people's vague intuitions suddenly found a clear framework.

I used to have a misconception, thinking that a C++ project's awesomeness depended on how flashy its template metaprogramming was or how sophisticated its build system was. Brainwashed by various "Modern C++ Best Practices," I thought a project should be equipped with a full set of sophisticated CMake scripts. The result? I built a few such projects, felt cool at the time, but came back a month later to modify code and found it wouldn't even compile—because a dependency upgraded and changed an interface, and that sophisticated script had a hardcoded version number. Stuck for half a day, I finally deleted the entire build directory and started over, wasting another two hours. This is actually doing myself a disservice.

The talk mentioned a key point: if your project is troublesome to build, requiring others to install four hundred global packages that conflict with their computer, you are blocking potential contributors. Many have had this experience—wanting to submit a PR to a famous C++ library to fix an obvious issue, but the README reads like a heavenly book, the dependency list is two pages long, and it requires specific versions of Boost and LLVM. After messing around all night without success, the next day you silently close that PR page and never go back. It's not that you don't want to contribute, it's that your patience is exhausted.

So when building a project, we should stick to a bottom line: for a person who knows nothing about the project, from `git clone` to running the first `hello world`, it shouldn't take more than five minutes. I verified this idea with a small tool I'm writing recently, and the effect was surprisingly good.

First, look at the directory structure, deliberately kept very flat:

```text
.
├── CMakeLists.txt
├── src
│   ├── main.cpp
│   └── utils.cpp
├── include
│   └── utils.h
├── README.md
└── .editorconfig
```

No submodules, no complex directory nesting. `CMakeLists.txt` is also written as straightforwardly as possible:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mytool
    src/main.cpp
    src/utils.cpp
)

target_include_directories(mytool PRIVATE include)
```

`README.md` was also rewritten. No longer the "feature list + bunch of badges" style, it directly tells how to run it:

```markdown
# MyTool

A simple tool to do X.

## Build

Requires CMake 3.20+ and a C++20 compiler.

```bash
git clone https://github.com/user/mytool.git
cd mytool
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```bash
./mytool
```

## Troubleshooting (踩坑记录)

- **Windows users**: If you see a link error, make sure you are using the Ninja generator.
- **Old GCC**: GCC 10 or older is not supported.

Note the final "Troubleshooting" section—I added this after stepping in pits myself. I used to think writing this kind of thing was "unprofessional." Now I think this is the most professional part. Because you are saving time for the next person, and saving time is the greatest kindness.

I asked two colleagues to test this project. One mainly writes Python, the other Java. Both got it running in three minutes. The Python colleague even said, "This is simpler than configuring the environment for many Python projects." For a C++ project to be praised for "simple configuration," that was unthinkable before.

The talk also mentioned a very forward-looking point: if you make your project easy to get into and out of, you are not only helping humans, but also helping AI agents. I've certainly felt this recently. When using Cursor to assist in coding, I found that if a project has a clear structure, few dependencies, and a simple build, the AI can understand more project context and give more reliable suggestions. Conversely, if the project has a bunch of nested custom compiler flags and implicit macro definitions, the AI often gives suggestions that "look right but don't actually run," because it doesn't understand what actually happened in that complex build environment.

Template errors give headaches to humans, and they give headaches to AI too—when it sees a template instantiation error stack two hundred lines long, the response is often generic. But if the project itself is clean and highly modular, error messages are much shorter, and AI (as well as humans) can locate problems much faster. So "design for humans" and "design for AI" are actually unified on this point: both are about reducing cognitive load.

Looking back, the principle is simple. We write code, ultimately for humans to read and use. The compiler only cares if the syntax is correct, but humans care about "can I quickly understand what this project does, and can I quickly fix it and leave." Making complex things simple is the real skill.

Finally, I get it—in the process of assembling C++ programs, those tools, libraries, and build systems are all parts, but the person holding those parts to do the assembly is the most important. If you ignore that, the most sophisticated parts are just a pile of scrap metal.

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
    chapter="C++17 standard library optional wrapper"
    url="https://en.cppreference.com/cpp/utility/optional"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Formatting library (std::format)"
    publisher="cppreference.com"
    :year="2020"
    chapter="C++20 formatting library, Python-style format strings"
    url="https://en.cppreference.com/cpp/utility/format"
  />
  <ReferenceItem
    :id="4"
    author="Google"
    title="Abseil C++ Common Libraries"
    publisher="Google LLC"
    :year="2017"
    chapter="Google open-source C++ common libraries, including absl::StatusOr, absl::Span, absl::string_view, etc."
    url="https://abseil.io/"
  />
  <ReferenceItem
    :id="5"
    author="Edison Design Group"
    title="EDG C++ Front End"
    publisher="Edison Design Group"
    :year="1994"
    chapter="Commercial C/C++ language front end, widely used in compilers and static analysis tools"
    url="https://www.edg.com/"
  />
  <ReferenceItem
    :id="6"
    author="Bjarne Stroustrup"
    title="Cfront — The Original C++ Compiler"
    publisher="AT&T Bell Labs"
    :year="1983"
    chapter="The earliest C++ compiler, translating C++ source to C code for compilation by a C compiler"
    url="https://en.wikipedia.org/wiki/Cfront"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="constexpr specifier (since C++11)"
    publisher="cppreference.com"
    :year="2011"
    chapter="constexpr functions are implicitly inline, allowing definition in headers without violating ODR"
    url="https://en.cppreference.com/cpp/language/constexpr"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="inline specifier"
    publisher="cppreference.com"
    :year="2011"
    chapter="The core semantic of inline is to allow the same function to be defined in multiple translation units without violating ODR"
    url="https://en.cppreference.com/cpp/language/inline"
  />
  <ReferenceItem
    :id="9"
    author="Free Software Foundation"
    title="C++ Module Mapper (GCC)"
    publisher="GNU Project"
    :year="2021"
    chapter="GCC module cache uses .gcm format, stored in gcm.cache directory"
    url="https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Module-Mapper.html"
  />
  <ReferenceItem
    :id="10"
    author="LLVM Project"
    title="Standard C++ Modules — Clang Documentation"
    publisher="LLVM Foundation"
    :year="2021"
    chapter="Clang uses .pcm (Precompiled Module) format to store module compilation artifacts"
    url="https://clang.llvm.org/docs/StandardCPlusPlusModules.html"
  />
  <ReferenceItem
    :id="11"
    author="cppreference.com"
    title="Modules (since C++20)"
    publisher="cppreference.com"
    :year="2020"
    chapter="C++20 module system: module declaration, global module fragment, export, import syntax"
    url="https://en.cppreference.com/cpp/language/modules"
  />
  <ReferenceItem
    :id="12"
    author="Kitware"
    title="CMake 3.28 Release Notes"
    publisher="Kitware Inc."
    :year="2023"
    chapter="C++20 named modules support, limited to Ninja and Visual Studio (VS 2022+) generators"
    url="https://cmake.org/cmake/help/latest/release/3.28.html"
  />
</ReferenceCard>

---

## Further Reading

- The core of the toolchain is compiler flags. To systematically organize common GCC/Clang compiler options and trade-offs, see [Volume 7: Compiler Options](../../../../vol7-engineering/02-compiler-options.md).
