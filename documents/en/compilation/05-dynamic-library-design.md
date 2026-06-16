---
chapter: 13
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
title: 'In-depth Understanding of C/C++ Compilation and Linking 6: A2 – Dynamic Library
  Design Basics – ABI Interface Design'
description: ''
translation:
  source: documents/compilation/05-dynamic-library-design.md
  source_hash: b49a1e6167a388ec60d512265ce40714e46e3bb3f9b401f3afa82b06e5c118e7
  translated_at: '2026-06-16T03:27:12.446415+00:00'
  engine: anthropic
  token_count: 2093
---
# In-depth Understanding of C/C++ Compilation and Linking Techniques 6——A2: Dynamic Library Design Fundamentals - ABI Interface Design

## Introduction

In this blog post, the author attempts to summarize and categorize some key technical points in the **design** of dynamic libraries, such as the design and export of binary interfaces.

## So, why involve the Binary Interface?

Essentially, the ultimate goal of designing a dynamic library (which the author believes must be kept in mind at all times) is to reuse our code for others to use. Therefore, we must consider the details of code collaboration. In a blog post a long time ago, we simplified the abstract concept of a dynamic library into an **interface** that specifies a number of exported symbols, written in header files or dedicated export files, to inform other users how to invoke the target functionality, and the underlying hidden details of machine code.

However, we know that what is written in human-readable files, such as function names and global variable names under classes in header files, is indeed an interface. But we obviously know that this does not count as a **binary interface**. All along, we seem to have been accustomed to the idea that as long as we export specified symbols and provide the machine code for the specific implementation, everything is worry-free. However, due to the free nature of C++ (note, the author did not say C; in fact, this problem erupts intensely in reusable libraries written in C++), the **processing from human-readable APIs to machine-compatible ABIs by different compiler vendors' implementations is inconsistent!** This has created a series of issues that are no laughing matter. Below, the author enumerates why and in which situations our C++ symbol export and ABI matching produce serious inconsistencies, causing trouble in software construction.

#### More Complex Naming Rules

The mapping from C++ functions to linker symbols is decided by the compiler vendor. Although some standards do exist to constrain compiler vendors to generate as universal symbols as possible, it is a pity that, taking g++ and MSVC as examples, there are still gaps. This means that a project using the MSVC compiler cannot directly and painlessly use the output of a project using the g++ compiler for the same symbol lookup (my other meaning is, if we don't adopt some means, we need to obtain the source code and recompile; the method we discuss later can finally avoid this approach).

Readers might ask: How does this happen? In fact, we can easily think of a series of code like this:

```c++
// 在C++中，我们很喜欢将一些方法放置到类中,
// OOP就是推介我们这样做的！
class Foo {
public:
    void someFunc(int a, const char* b);
};

// 或者，我们喜欢放置一些工具类的函数到单独的命名空间中
namespace charlies_tools {
   std::vector<std::string_view> split(const std::string& waited_splits, const char ch);
   std::vector<std::string_view> split(const std::string& waited_splits, const std::string_view sp_view);
};

```

As C++ programmers, we will naturally use these features to avoid some symbol-level conflicts and improve better readability in software engineering.

Let's look at how the symbol names produced by g++ compilation look:

```text

0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc

```

Then let's look at those produced by MSVC:

```text

00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))

```

In fact, we can see that the symbols written into the relocatable file look completely different, which means we cannot generalize our symbols at all. In addition, we have a series of features like overloading that allow us to provide the same function name with different parameter lists to coexist in an object file, forcing our toolchain to spend effort dealing with these issues.

This modification is called Name Mangling. Great, now we have to deal with these annoying problems.

#### Static Data Initialization Issues

In the C language, our data can often be considered trivial (aha, I like C too, at least it's controllable). Due to legacy code reasons, we are used to initializing these variables at the linking stage. However, in C++, we know that this data can be objects, which means there are calls to constructors. If these objects are **under the condition of irrelevant initialization timing** (that is, these objects do not form dependencies, meaning we don't have to initialize static object A before static object B), it actually doesn't matter. But the fear is the existence of timing-dependent static objects. Because the program runs on the CPU, the initialization order of these objects often has no fixed constraints, making it very easy to cause random program crashes.

Of course, this problem is easy to handle. We know that the initialization of data freely scattered in the data segment is uncertain, but if we put it in a function, then only when execution reaches that point do we initialize the object. Therefore, if static object A indeed needs to be initialized before static object B, we can do this:

```cpp
static void init_a_and_b() {
    static A network_instance;
    static B authentic_networks;
}

auto dummy = [](){
    init_a_and_b();
    return 0;
}();

```

## So, how to design a binary interface with less trouble?

#### Design C-style Export Interfaces

Of course, you don't have to act exactly like a C programmer to prevent conflicts or adopt C naming habits. What is being said here is not to export ABI symbols with distinct C++ characteristics. The way is to decorate the symbols you decide to export with the `extern "C"` identifier.

```cpp

#ifdef __cplusplus
extern "C"{
#endif

    int functional_a(int a, int b);

#ifdef __cplusplus
}
#endif

```

This way, we can make the interface seen by the linker look much cleaner.

#### Provide a Header File with Complete ABI Declarations

Here, **"providing a header file with complete ABI declarations"** refers to a header file (`.h`) that contains all necessary declarations, enabling the compiler to **fully understand** the interface of a library or module, thereby allowing it to:

1. **Correctly compile** code that calls the library.
2. **Correctly generate** machine code that interacts with functions in the library.

The core of this "complete ABI declaration" is that it includes not only function names but also all details that affect binary-level interaction. Therefore, we have the saying—provide a header file with complete ABI declarations. Below, we discuss what a header file providing complete ABI declarations contains:

##### Function Declarations

This is the most basic part. It tells the compiler the function's name, return type, and parameter types.

```cpp
// 不完整的声明 - 只知道名字和类型，但可能隐藏问题
int do_something(int a, int b);

// 更完整的声明 - 增加了extern "C"和异常规范
extern "C" int do_something(int a, int b) noexcept;

```

##### Type Definitions

If custom structures or classes are used in the interface, their memory layout must be explicit.

```cpp
// 完整的结构体声明，编译器能确定其大小和内存布局
struct MyData {
    int id;
    double value;
    char name[32];
};

// 函数使用这个结构体
extern "C" void process_data(const MyData* data);

```

If the header file does not have the complete definition of `MyData`, the compiler does not know how much `sizeof(MyData)` is, and cannot correctly allocate stack space or pass parameters for the `process_data` function call.

##### Macro and Constant Definitions

Used to define magic numbers or configurations used in the interface.

```cpp
#define MAX_BUFFER_SIZE 1024
#define LIB_VERSION 0x00010002

extern "C" int initialize_lib(int buffer_capacity = MAX_BUFFER_SIZE);

```

##### Including Other Header Files

If declarations depend on other types (such as standard library `size_t` or custom types), the corresponding header files need to be included.

```cpp
#include <stddef.h> // 为了使用 size_t

extern "C" void* allocate_buffer(size_t size);

```

# Reference

## Confirming the Name

If you want to see the symbol differences produced by the MSVC compiler and the g++ compiler yourself, the author will explain here how the results above were produced.

The MSVC compiler version used by the author is 19.44.35217, and the g++ version is 15.2.1.

We write the sample code above into test.cpp.

```cpp
#include <string>
#include <string_view>

class Foo {
public:
 void someFunc(int a, const char* b);
};

namespace charlies_tools {
void split(const std::string& waited_splits, const char ch);
void split(const std::string& waited_splits, const std::string_view sp_view);
};

void Foo::someFunc(int a, const char* b) { }
void charlies_tools::split(const std::string& waited_splits, const char ch) { }
void charlies_tools::split(const std::string& waited_splits, const std::string_view sp_view) { }

```

Then, on a Linux machine, use the `-c` command to translate test.cpp into machine code only:

```bash

g++ -c test.cpp -o test_name

```

Then, use the `nm` command to view the ABI.

```text

[charliechen@Charliechen runaable_dynamic_library]$ nm test_name
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc

```

This obtains the results listed in the main text.

For MSVC, you need to open the VS Developer Prompt to initialize the MSVC toolchain environment. Then, assuming you still save the code to test.cpp, use the `cl` compiler, specifying the compile-only flag and the latest C++ standard flag, to get the following output:

```text

D:\DownloadFromInternet>cl /c /std:c++latest test.cpp
用于 x86 的 Microsoft (R) C/C++ 优化编译器 19.44.35217 版
版权所有(C) Microsoft Corporation。保留所有权利。

/std:c++latest 作为最新的 C++
working 草稿中的语言功能预览提供。我们希望你提供有关 bug 和改进建议的反馈。
但是，请注意，这些功能按原样提供，没有支持，并且会随着工作草稿的变化
而更改或移除。有关详细信息，请参阅
https://go.microsoft.com/fwlink/?linkid=2045807。

test.cpp

```

Subsequently, using the `dumpbin` utility, we get:

```text

D:\DownloadFromInternet>dumpbin /SYMBOLS test.obj
Microsoft (R) COFF/PE Dumper Version 14.44.35217.0
Copyright (C) Microsoft Corporation.  All rights reserved.

Dump of file test.obj

File Type: COFF OBJECT

COFF SYMBOL TABLE
000 01058991 ABS    notype       Static       | @comp.id
001 80010191 ABS    notype       Static       | @feat.00
002 00000003 ABS    notype       Static       | @vol.md
003 00000000 SECT1  notype       Static       | .drectve
    Section length  178, #relocs    0, #linenums    0, checksum        0
005 00000000 SECT2  notype       Static       | .debug$S
    Section length   74, #relocs    0, #linenums    0, checksum        0
007 00000000 SECT3  notype       Static       | .bss
    Section length    4, #relocs    0, #linenums    0, checksum        0, selection    2 (pick any)
009 00000000 SECT3  notype       External     | __Avx2WmemEnabledWeakValue
00A 00000000 SECT4  notype       Static       | .text$mn
    Section length   25, #relocs    0, #linenums    0, checksum E54AE742
00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))
00F 00000000 SECT5  notype       Static       | .chks64
    Section length   28, #relocs    0, #linenums    0, checksum        0

String Table Size = 0x123 bytes
  Summary
           4 .bss
          28 .chks64
          74 .debug$S
         178 .drectve
          25 .text$mn

```
