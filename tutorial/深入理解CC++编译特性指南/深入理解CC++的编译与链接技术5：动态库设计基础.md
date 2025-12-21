# 深入理解C/C++的编译链接技术6——A2：动态库设计基础之ABI设计接口

## 前言

在这片博客中，笔者尝试的是总结和归纳一部分咱们动态库**设计**中一些比较重要的技术要点，比如说，二进制接口的设计导出。

## 所以，为什么扯上二进制接口了

本质上，我们设计动态库的最终的目的（笔者认为这个是要时刻牢记的），是将我们的代码复用给其他人进行使用。那么，代码协作细节就是我们要考虑的。我们在很久之前的博客中，就化简了动态库这个抽象概念为指定若干导出符号的，写在了头文件或者是专门的导出文件，供其他用户知晓如何调用起来目标功能的**接口**，和背后的若干隐藏的具体细节机器码。

但是，我们知道写在人类可读文件的，比如说头文件中的若干类下的函数名称和全局变量名称，这的确是接口，但是我们显然知道这不算**二进制接口**，一直以来我们似乎习惯了只要我们导出了指定符号和提供了具体实现的机器码，好像一切都高枕无忧了。但是，由于C++自由的特性（注意到，笔者没有说C，事实上，这个问题集中爆发在C++所编写的可复用库上），导致**不同的厂商实现的编译器所处理的从人类可读的API，到机器对接的ABI**是不一致的！这就产生了一系列一点都让人笑不出来的问题。笔者下面枚举为什么，哪些情况，让我们的C++的符号导出和ABI对接，产生了严重的不一致问题从而造成软件构建的麻烦。

#### 更加复杂的Naming规则

C++函数到链接器符号之间的映射，是编译器厂商决定的，尽管的确存在一些标准，约束我们的编译器厂商产生尽可能通用的符号，但是很遗憾，以g++和msvc为例子，还是存在一些差距，导致同一个符号的查找映射规则，采用了MSVC编译器的工程无法无痛的直接给采用了g++编译器的工程使用（我的另一个意思是，如果不采用一些手段，咱们就需要拿到源码重新编译，我们后面讨论的方法终于可以回避掉这种做法）。

读者同志们会发问：这是怎么回事呢？其实，我们很容易想到这样一系列的代码：

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

我们作为C++程序员，会很自然的使用到这些特性，回避掉一些符号层次的冲突问题，提升软件工程中更好的可读性。

我们来看看g++编译产生的符号名称是如何的：

```
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc
```

然后我们再看看MSVC产生的：

```
00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))
```

实际上，我们可以看到，写入到可重定位文件中的符号,长的完全不一样，说明我们根本没法通用我们的符号。除此之外，我们还有重载等一系列功能，允许我们提供相同的函数名，不同的参数列表可以共存到一个目标文件的技术，导致我们的工具链不得不费心思处理这些问题。

这种修饰被称为名称修饰（Name Mangling），很好，这下我们不得不处理这些糟心的问题了。

#### 静态区数据初始化问题

C语言中，我们的数据往往可以被认为是平凡的（啊哈，换我我也喜欢C，至少可控），出于遗留代码缘故，我们习惯在链接阶段，就初始化这些变量。但是在C++中，我们知道，这些数据可以是对象，意味着存在构造函数的调用。如果说，这些对象的都是在**初始化时序无关的条件下**（即，这些对象不构成依赖，不会说我们一定要先初始化静态对象A，才能初始化静态对象B），其实也没关系。但是就怕存在时序相关的静态对象，因为程序上CPU运行，这些对象的初始化顺序，往往没有固定的约束，导致很容易造成程序的随机崩溃。

当然，这个办法很好处理，我们知道，自由散布在data段的数据的初始化时不确定的，但是如果我们放到一个函数中，此时，只有在执行到的时候，我们才会初始化对象。由此，假如说，静态对象A的确要在静态对象B前初始化，我们可以这样做：

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

## 所以，如何设计少麻烦的二进制接口

#### 设计C风格的导出接口

当然，您大可不必真的像C程序员那样防止冲突，采用C命名的习惯，这里说的是不要导出C++特色各异的ABI符号规则。办法就是将您决定导出的符号，修饰上extern "C"标识。

```cpp

#ifdef __cplusplus
extern "C"{
#endif

    int functional_a(int a, int b);
    
#ifdef __cplusplus
}
#endif
```

这样我们就能让链接器所看到的接口看起来干净很多。

#### 提供完整ABI声明的头文件

这里**“提供完整ABI声明的头文件”** 指的是一个头文件（`.h`），它包含了所有必要的声明，使得编译器能够**完全理解**一个库或模块的接口，从而能够：

1. **正确编译** 调用该库的代码。
2. **正确生成** 与库中函数交互的机器码。

这个“完整的ABI声明”的核心在于，它不仅仅包括函数名，还包括所有影响二进制层面交互的细节。所以，我们才会有说法——提供完整ABI声明的头文件。下面我们讨论一下，一个提供完整ABI声明的头文件包含啥：

##### 函数声明

这是最基本的部分。它告诉编译器函数的名字、返回类型和参数类型。

```cpp
// 不完整的声明 - 只知道名字和类型，但可能隐藏问题
int do_something(int a, int b);

// 更完整的声明 - 增加了extern "C"和异常规范
extern "C" int do_something(int a, int b) noexcept;
```

##### 类型定义

如果接口中使用了自定义结构体或类，其内存布局必须明确。

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

如果头文件里没有`MyData`的完整定义，编译器就不知道`sizeof(MyData)`是多少，无法正确地为`process_data`函数调用分配栈空间或传递参数。

##### 宏和常量定义

用于定义接口中使用的魔法数字或配置。

```cpp
#define MAX_BUFFER_SIZE 1024
#define LIB_VERSION 0x00010002

extern "C" int initialize_lib(int buffer_capacity = MAX_BUFFER_SIZE);
```

##### 包含其他头文件

如果声明依赖于其他类型（如标准库的`size_t`或自定义类型），需要包含相应的头文件。

```cpp
#include <stddef.h> // 为了使用 size_t

extern "C" void* allocate_buffer(size_t size);
```









# Reference

## 确认名称

如果您想亲自看看MSVC编译器和g++编译器产生的符号差异，笔者这里说明一下上面的结果是如何产生的

笔者使用的MSVC编译器版本是19.44.35217，g++版本是15.2.1

我们将上面的样例代码写入test.cpp中

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

然后，在Linux机器上，利用-c指令只翻译test.cpp为机器码：

```
g++ -c test.cpp -o test_name
```

然后，利用nm指令查看ABI

```
[charliechen@Charliechen runaable_dynamic_library]$ nm test_name
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000022 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000000 T _ZN3Foo8someFuncEiPKc
```

这就得到了笔者在正文中列出的结果

对于MSVC，您需要打开VS Develop Prompt初始化MSVC工具链环境，然后，我们依旧假设您保存代码到test.cpp中，然后，利用cl编译器，指定仅编译的标志和最新C++标准标志，可以得到下面的输出

```
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

随后，利用dumpbin小工具，得到：

```
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

