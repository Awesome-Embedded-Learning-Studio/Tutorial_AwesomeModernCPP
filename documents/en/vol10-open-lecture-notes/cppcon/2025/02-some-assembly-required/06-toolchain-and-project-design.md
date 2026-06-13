---
title: Compilers, Toolchains, and Project Design Baselines
description: 'CppCon 2025 Talk Notes —— C++: Some Assembly Required by Matt Godbolt'
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
  source_hash: 48cea3f59be3f3407d4ebe4296f2a3d9d0c0984515c0903ff237dd833323c7b4
  translated_at: '2026-06-13T11:48:31.424972+00:00'
  engine: anthropic
  token_count: 3268
---
# The C++ Assembly Project: Compilers, Toolchains, and "Non-Standard but Excellent" Libraries

Many programmers' understanding of the C++ ecosystem stops at "the language itself plus the standard library"—write code, compile, run, done. But if we walk through the entire engineering workflow, we realize that the C++ language itself is just a small piece of the whole project. To actually assemble a set of components into something that runs, we need much more than just C++ syntax. Today, I want to discuss this "assembly" process and the infrastructure that supports it.

## First, a Correction: Not All Good Things Enter the Standard

Many people have a deep-seated misconception that if a library is good enough and important enough, it "should" be included in the standard library. For example, seeing `std::optional` enter C++17<RefLink :id="2" preview="std::optional (C++17)" /> and `std::format` enter C++20<RefLink :id="3" preview="std::format (C++20)" />, they take it for granted that this is the destination for all excellent libraries. But in reality, that's not how it works at all.

The standardization process has its own logic and thresholds. Some library patterns may simply be unsuitable for the standard, or the maintainers never intended to send them there—they exist as independent, high-quality libraries that you can just use directly. The most typical example is Abseil<RefLink :id="4" preview="Google Abseil C++ Libraries" />. This set of C++ libraries open-sourced by Google contains many very practical components, like enhanced versions of `optional`, `span`, and `string_view`. They haven't entered the standard, nor do they need to, but their quality is extremely high, and they are used in many production environments.

Another point worth noting: It's not only massive projects backed by big companies that can enter the standard. Small alliances or even individuals, as long as their proposal quality is solid and the argument is sufficient, can get code into the standard. Of course, alliances formed by GPU vendors and large HPC institutions do have strong push on the standard, so things like parallel computing and SIMD have advanced particularly quickly. But the key is that the channel is open; it's not just for giants.

So the correct mindset should be: Stop staring at the standard library waiting for "official solutions," and instead actively seek out those mature, high-quality third-party libraries. Although the C++ ecosystem isn't as centralized as Rust's crates.io and finding libraries is indeed a bit harder, the good stuff is out there.

## The Real Assembly Starts After the Code is Written

Okay, let's assume we've selected our components and written the code. What's next? Turning C++ code into an executable file requires much more than just C++.

First, we need a compiler. We are actually quite lucky now to have three major players: GCC, Clang, and MSVC, plus EDG<RefLink :id="5" preview="EDG C++ Front End" /> (mainly used for standard compliance testing and certain commercial scenarios). These compilers are high quality, and some of them are open-source projects maintained by the community. You might take this for granted, but looking back at history shows how far we've come.

The earliest C++ compilers were essentially Cfront<RefLink :id="6" preview="Cfront: The Original C++ Compiler" /> written by Bjarne Stroustrup—a C++ to C translator. It took C++ code, converted it into C code, and then used a normal C compiler to compile that intermediate product. C++ was initially "parasitic" on C's compilation infrastructure.

Now, of course, it's completely different. GCC and Clang both have mature C++ frontends, and support for various standard versions is getting better and better. My current main environment is GCC 16.1.1 on Arch Linux WSL, with Clang 17 for cross-validation, and occasionally MSVC 19.38 on Windows to ensure cross-platform compatibility. I've stepped into quite a few pits with toolchain versions; I'll write a separate post about that later.

But the compiler is just the first step. After compiling individual translation units into object files, we need a linker to stitch them together. Many people have used C++ for years without giving the linker a second thought—because in most cases, a single `g++` command handles it, and the linker works silently in the background, unnoticed. It's not until you encounter a weird ODR (One Definition Rule) violation causing a linker error—where an inline function expands into different versions in two translation units, and the linker reports an incomprehensible symbol conflict—that you realize how complex and important the linker really is.

The core point is: When complaining that "C++ is hard to use," often what you're actually complaining about isn't the C++ language itself, but some part of this assembly process. It might be the compiler spitting out a screen full of unintelligible template errors, or the linker not finding symbols, or not knowing how to integrate third-party libraries correctly. If we break down these steps, each has corresponding tools and solutions; they are just scattered around and need to be assembled yourself.

## A Simple Example to Experience "Assembly"

Here is a very small example. It doesn't involve any complex logic; it just demonstrates what the compiler and linker are doing respectively in the process of turning "multiple source files" into "one executable file."

First is the header file `math_utils.h`, just declaring a function:

```cpp
// math_utils.h
#pragma once
int add(int a, int b);
```

Then is another header file `utils.h`, which depends on the `add` above:

```cpp
// utils.h
#pragma once
#include "math_utils.h"
void print_add(int a, int b);
```

Finally, `main.cpp`:

```cpp
// main.cpp
#include "utils.h"
int main() {
    print_add(1, 2);
    return 0;
}
```

This example is so simple it's silly, but it's perfect for demonstrating the step-by-step execution of the compilation process. You can manually control every step with the following commands:

```bash
# Step 1: Preprocess (stop after preprocessing)
g++ -E main.cpp -o main.ii

# Step 2: Compile to assembly (stop after compilation, skip assembly)
g++ -S main.cpp -o main.s

# Step 3: Assemble to object file
g++ -c main.cpp -o main.o
g++ -c utils.cpp -o utils.o

# Step 4: Link object files to executable
g++ main.o utils.o -o my_app
```

If you use `cat` to look at the preprocessed `main.ii` file, you'll see the contents of `stdio.h` and `math_utils.h` have all been expanded into it. This is why function definitions in header files need `inline` or `constexpr`<RefLink :id="7" preview="constexpr implicitly inline" />—otherwise, if two different `.cpp` files include the same header file, the linker will see two copies of the function definition and report an ODR violation directly.

A common misconception about `inline` exists: many people think it's just a hint to "suggest the compiler inline." But actually, `inline`'s true role in C++ is to allow the same function to be defined in multiple translation units without violating the ODR<RefLink :id="8" preview="inline keyword and ODR exemption" />. Inline optimization is whatever the compiler wants to do; it has no necessary relationship to whether you say `inline` or not.

## Compiler Selection: Current Practice

Daily development is basically GCC-centric, with Clang as a backup. The reason is simple: GCC has the best ecosystem on Linux, and I'm familiar with its error messages; Clang's error hints are indeed friendlier than GCC in some scenarios (especially templates), so when I encounter an error I don't understand, I switch to Clang to compile again, looking at the problem from another angle.

```bash
# Compile with GCC
g++ main.cpp -o main_gcc -Wall -Wextra

# Compile with Clang
clang++ main.cpp -o main_clang -Wall -Wextra
```

I strongly recommend forming this habit. For the same compilation error, GCC might spit out a screen of template instantiation backtraces, while Clang can sometimes point out the problem in a more concise way. The reverse is also true; sometimes GCC is clearer. Cross-validating with two compilers can save a lot of time.

I use MSVC less, but if the project needs to be cross-platform, compiling with MSVC on Windows occasionally is very necessary. Different compilers occasionally have subtle differences in interpreting the standard; discovering them earlier is better than having problems after going live.

---

# Editors and Build Systems: From "Just Works" to the Pitfalls of Modules

## Editors: Please Help Me Understand This Code

Regarding editor selection, many people have indeed taken a long detour. When I started learning C++, I used VS Code with a rudimentary C/C++ plugin. Code completion took forever to pop up, and error messages were always red squiggles that didn't speak human. I even thought "C++ development is just like this; editors can't help you much." Later, seeing CLion's code completion, refactoring, and real-time static analysis, I realized—it's not that C++ is bad, it's that the tools were bad.

But I don't want to start an "editor war" here. I just want to say one thing: **Never mix spaces and tabs**. I once took over a project where spaces and tabs were mixed. The indentation looked completely normal in the editor, but once pushed to CI, the formatting was all messed up, and error lines didn't match the actual code. Since then, I always configure `.editorconfig` in projects to unify spaces, leaving no room for mixing.

Speaking of the editor ecosystem, we are actually at a very interesting stage now. Terminal Vim/Neovim users can achieve an experience very close to an IDE via clangd + LSP, with code completion, go-to-definition, and hover docs all available. But personally, CLion is ready-to-use with native-level CMake integration. Create a new project, configure `CMakeLists.txt`, click run, and it goes—no need to spend two days configuring the editor. Time should be spent understanding C++, not configuring the editor.

However, I've recently encountered a scenario more and more frequently where no editor can help. I write a piece of complex logic using several lambdas for callback registration. It feels very clear when writing it, but three days later, looking back, I have no idea what that code is doing. I even pasted the code to CLion's built-in AI assistant to explain it, and after reading the explanation, I still only half-understood. What does this show? It shows that tools can help you write code and find bugs, but they can't help you **think**. Code readability ultimately relies on the design of abstraction layers; I've stepped in this pit too many times.

## Build Systems: Thought CMake Was Hard, Until I Touched Modules

If the editor is the "writing experience," then the build system is the "running experience," and in C++, well, this experience often makes you want to smash your keyboard.

I used to think CMake was torture enough. What kind of argument passing, whether to use `target_link_libraries`, `target_include_directories`, or `target_compile_options`, how to troubleshoot when `find_package` can't find a package—it took more than half a year to get proficient. But as hard as CMake is, it's at least something you can "learn and get started with," and although the documentation reads like a heavenly book, at least there is documentation.

Until I tried C++20 Modules. When I first heard about Modules, I was excited, thinking finally I wouldn't have to suffer the slow compilation speed of header inclusion. Then I tried it—first of all, CMake's support for Modules in early versions was very rough. You had to manually specify how `.cpp` files compile into module interface units vs. module implementation units. Module file formats differed between compilers: GCC uses `.gcm`<RefLink :id="9" preview="GCC module cache .gcm" />, Clang uses `.pcm`<RefLink :id="10" preview="Clang precompiled module .pcm" />, and MSVC uses another set. Then you hit circular dependency issues. In the traditional header era, you could use forward declarations to break circular dependencies, but in the Modules world, this approach isn't quite the same. I was stuck on this pit for three days, finally realizing my understanding of "module partitions" was simply wrong.

Here is a minimal runnable example I折腾 out at the time. The example itself isn't complex, but getting it working took a whole weekend:

```cpp
// math.ixx (module interface)
export module math;

export int add(int a, int b) {
    return a + b;
}
```

```cpp
// import math module and use it
import std;
import math;

int main() {
    std::cout << "3 + 5 = " << add(3, 5) << std::endl;
    return 0;
}
```

```cmake
cmake_minimum_required(VERSION 3.28)
project(MathModuleExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MODULE_EXPERIMENTAL YES "YES" "NO" "NO")

add_executable(app
    main.cpp
)

# CMake 3.28+ handles module dependencies automatically if configured correctly
target_sources(app PUBLIC
    FILE_SET CXX_MODULES FILES math.ixx
)
```

You see, the code itself is very intuitive. `export` marks what is visible, `import` replaces `include`, and conceptually it's much cleaner than headers. But to get these few lines running, you need CMake 3.28 or above, sufficient compiler support for C++20 modules, and the configuration in `CMakeLists.txt` must be correct. I initially tried with CMake 3.25 and got an error saying it couldn't find the module. I was stuck for two hours before realizing it was a version issue.

There's another easily overlooked limitation: CMake 3.28's support for C++20 modules is limited to the Ninja generator and Visual Studio 2022 and above<RefLink :id="12" preview="CMake 3.28 module support generator limitations" />. Using the traditional Makefile generator currently doesn't work. This is a relatively hidden pit; you remember it once you step in it.

And this is just the simplest case—single module, no partitions, no dependencies on other modules. Once the project scales up, modules import each other, and deriving the build order becomes a nightmare. After talking to quite a few people, I found everyone has tripped over Modules build configuration; this isn't an isolated case.

---

# Designing for Humans: The Bottom Line of Project Design

When hearing the talk about "designing for humans," many people's vague intuitions suddenly found a clear framework.

I used to have a misconception, thinking that whether a C++ project is awesome depends on how flashy its template metaprogramming is or how sophisticated its build system is. After being brainwashed by various "Modern C++ Best Practices," I thought a project should be equipped with a full set of sophisticated CMake scripts. The result? I built a few such projects, felt cool at the time, but came back a month later to modify code and found it wouldn't even compile—because a dependency upgraded and changed an interface, and there was a hardcoded version number in that sophisticated script. I was stuck for half a day, finally deleting the whole build directory and starting over, wasting another two hours. This is actually doing myself a disservice.

The talk mentioned a key point: If your project is troublesome to build, requiring others to install four hundred global packages that conflict with their computer, you are blocking potential contributors. Many people have had this experience—wanting to submit a PR to a famous C++ library to fix an obvious problem, but the README reads like a heavenly book, the dependency list is two pages long, and it requires specific versions of Boost and LLVM. After messing around all night without getting it to run, the next day I silently closed that PR page and never went back. It's not that I didn't want to contribute, it's that my patience was exhausted.

So when building a project, we should stick to a bottom line: For a person who knows nothing about the project, the time from `git clone` to running the first "hello world" should not exceed five minutes. I verified this idea with a small tool I'm writing recently, and the effect was surprisingly good.

First, look at the directory structure, deliberately kept very flat:

```text
my_tool/
├── src/
│   ├── main.cpp
│   └── utils.cpp
├── include/
│   └── utils.h
├── CMakeLists.txt
└── README.md
```

No submodules, no complex directory nesting. `CMakeLists.txt` is also written as straightforwardly as possible:

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyTool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(my_tool src/main.cpp src/utils.cpp)
target_include_directories(my_tool PRIVATE include)
```

`README.md` was also rewritten. No longer a "feature list + bunch of badges" style, it directly tells how to run it:

````markdown
# MyTool

A simple tool to do X.

## Build

Requires CMake 3.15+ and a C++17 compiler.

```bash
git clone https://github.com/user/my_tool.git
cd my_tool
mkdir build && cd build
cmake ..
cmake --build .
./my_tool
```

## Pitfalls

If you see `error: 'filesystem' not found`, try adding `-std=c++17` manually or upgrading GCC.
````

Note the "Pitfalls" section at the end—I added this after stepping in a pit myself. I used to think writing this kind of thing was "unprofessional," but now I think this is the most professional part. Because you are saving time for the next person, and saving time is the greatest kindness.

I asked two colleagues about this project, one mainly writing Python and one mainly writing Java. Both got it running within three minutes. The Python colleague even said, "This is simpler than configuring the environment for many Python projects." For a C++ project to be praised for "simple configuration," that was unthinkable before.

The talk also mentioned a particularly forward-looking point: If you make your project easy to enter and exit, you are not only helping humans, but also helping AI agents. I've definitely felt this recently. When using Cursor to assist in coding, I found that if a project has a clear structure, few dependencies, and simple builds, the AI can understand more project context and give more reliable suggestions. Conversely, if the project has a bunch of nested custom compiler flags and implicit macro definitions, the AI often gives suggestions that "look right but don't actually run," because it doesn't understand what's really happening in that complex build environment.

Template errors give me a headache, and AI gets a headache too—when it sees a template instantiation error stack two hundred lines long, the response is often generic. But if the project itself is clean and highly modular, error messages are much shorter, and AI (as well as humans) can locate problems much faster. So "designing for humans" and "designing for AI" are actually unified on this point: both are about reducing cognitive load.

Looking back, the principle is simple. We write code, ultimately for people to read and for people to use. The compiler only cares if the syntax is correct, but people care about "can I quickly understand what this project does, and can I quickly fix it and leave." Making complex things simple is the real skill.

Finally, I get it—in the process of assembling a C++ program, those tools, those libraries, and those build systems are all parts, but the person holding those parts and doing the assembling is the most important. If you ignore that, the most sophisticated parts are just a pile of scrap metal.

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
    chapter="C++17 standard library optional value wrapper"
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
    chapter="Commercial C/C++ language frontend, widely used in compilers and static analysis tools"
    url="https://www.edg.com/"
  />
  <ReferenceItem
    :id="6"
    author="Bjarne Stroustrup"
    title="Cfront — The Original C++ Compiler"
    publisher="AT&T Bell Labs"
    :year="1983"
    chapter="The earliest C++ compiler, translating C++ source to C code for C compiler compilation"
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

- The core of the toolchain is compiler flags. To systematically organize common GCC/Clang compiler options and trade-offs, see [Volume 7 · Compiler Options](../../../../vol7-engineering/02-compiler-options.md).
