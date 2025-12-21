# 深入理解C/C++的编译与链接技术：导论

## 前言

​	这个是一个新的系列！是笔者本周打算系统深入开展研究的话题。具体来讲，我们会讨论和总结一系列的C/C++编程中，我们很有可能一带而过但是肯定被备受折磨的话题——编译与链接技术。我相信任何一个朋友都遇到过令人头疼的`undefined referenced`等问题，我相信看到这样的报错不少朋友会吓得一激灵（笔者前段时间就被模板实例化时的`undefined referenced`折磨过）。

​	解决这类问题，我相信不少朋友最开始的时候都是手忙脚乱的问AI，上网搜，但是鲜有人真正思考——为什么我们会有`undefined referenced`这类的错误呢？抛去那些咱们真的在构建系统中真忘记提供源代码文件的情况（我相信很多人也遇到过，笔者也是），很多情况时咱们真的有——起码真的是自己认为自己有的——提供了源文件且你甚至看到他链接了，但是就是链接失败了。

举个例子，比如说您在一个lib.c文件中编写了，并且将它制作成了一个静态库libutils。

```c
int int_max(int a, int b) {
	return a > b ? a : b;
}
```

随后，我们立马在一个C++文件使用了`int_max`

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
	int a = 1, b = 2;
	std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}
```

随后，我们敲下这段指令期待自己的程序成功编译的时候，我们得到了一个非常奇怪的错误——

```
[charliechen@Charliechen linkers]$ g++ usage.cpp -L. -lutils -o usage
/usr/sbin/ld: /tmp/ccdSskJz.o: in function `main':
usage.cpp:(.text+0x88): undefined reference to `int_max(int, int)'
collect2: error: ld returned 1 exit status
[charliechen@Charliechen linkers]$ 
```

​	这看起来太奇怪了，我们明明链接了libutils，他甚至都找到了我们的libutils（没有抱怨`/usr/sbin/ld: cannot find -lutils: No such file or directory`，这就是找到了），但是为什么会出错呢？而且就算没找到这个符号，为什么不在编译的时候就向我们抱怨呢？我认为，如果你像[`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html)的作者所说的那样，立马看到其中的问题的时候，我想这篇导论性质的《深入理解C/C++的编译与链接技术：导论》对您是没有新鲜东西的，我们随后才会真正细致的聊每一个细节，这里不会。

​	**本篇博客可能需要您至少写过C语言程序（上面的问题尽管涉及到C++，但是本文的核心不在C++），如果您遇到过类似`undefined referenced`的错误而不知道如何解决，那更好了**

## 所以，我们写的变量和函数到底意味着什么？

​	这个问题没有问**你**，这个问题我们在问**计算机**，为了回答上面一连串你可能从来都没有想过的问题，在那之前，我们现在必须要回答一个问题——“我们找到的和找不到的这些东西，计算机是怎么知道的？”，更加规范的问——编译器工具链是如何搜集到和查找到符号的？如何进一步的转化成更加好处理的形式（比如说，咱们把函数映射成了计算机可以找到的地址，这样，熟悉汇编的朋友立马就能想到函数是如何工作的——函数名转化为地址后call（调用）对应的地址即可，计算机的处理流自动跳转到对应地址取指令开始执行代码）。终归而言，我们的第一步是——我们能理解的，表达业务含义的变量和函数，如何转化成机器可以知道的哪里是哪里的地址的？中间的处理是如何的？**我们写的变量和函数到底对计算机而言意味着什么？**

​	任何一个计算机方向的学生毫无疑问的能立马说出程序从源代码文件到上操作系统跑起来的四个经典步骤——预处理，编译，链接，和**执行**（会有人问这不是废话嘛？为什么执行单独说？好问题！动态库的动态加载和启动加载我们会好好说的）。

​	我们想要回答好上面的问题，就要重点关心后三个内容（预处理是**源代码向源代码的转换**，比如说#define的展开和基于#if条件选择编译技术的，我们在这里不会谈论）。

​	在我们编写C语言文件的时候——不管是B站的授课UP主们， 还是大佬博客的笔记，还是你的大学老师昏昏欲睡的念他那陈年的PPT，都会告诉你。编写C语言文件我们无非在做两个事情——声明和实现。我们讨论的对象是**全局变量和函数**，这一点我必须放在这里强调。

- 局部变量呢？啊，讨论这个没有意义，他们是程序上CPU后，整个操作系统后端动态为您的程序代码服务的——可能是**分配给具体的寄存器，也有可能是分配内存，但是绝对不躺在磁盘的可执行文件上！**
- 特别值得一提的是——实现包含了声明。不太理解？举个例子，您想您都告诉A是啥了，我是不是同时也告诉您这里有一个A了？

​	声明很简单，我们只是大声的嚷嚷这里存在一个东西（），你问我那是什么？值是多少？不好意思我不知道，我只能告诉你的确存在这个东西，在哪里编译器你自己去找。

​	实现也不难，我们把一个声明（可能是上面我们说的别处我们吵吵的声明，也有可能是就地声明，比如说`int a = 2`）和这个声明的实现关联起来。这个动作就是**实现**。对于全局变量，这个实现是一个数据。对于函数，这个则是我们的执行代码。一个全局变量的实现会让编译器在之后生成的可执行文件中，给您的变量分配具体的空间。当然，还少不了您赋予的值，不然您实现干啥是不是？

​	我们知道，在编译后生成的可重定位文件（Locatable Objects）会暴露出来函数名称和变量。我们在编写程序的时候，就下意识的认为他们可以被找到（敏锐的朋友立马打断我——什么可以找到？编译期间还是链接运行期间的？不着急，马上聊）——这个在严肃的学术讨论中叫做**符号的可见性**。**可见的符号是可访问的！**这里的**可见符号的可访问性**需要二分的讨论：

- 编译期间的可访问——比如说那些在C语言程序中**没有被static修饰的符号，包括全局变量和函数**，您写过C语言程序，显然知道在`a.c`中写下全局的`static int a = 1;`和`static int max(int a, int b){return a > b ? a : b;}`后，`b.c`完全访问不到！您可以自己试一试。
- 运行期间的可访问——这里说的就是整个全局变量和函数，不管有没有被static修饰。因为他们都存储在可执行文件中，上CPU之后，操作系统必然要为不管是否为static修饰的全局变量和函数分配程序生命周期长度的内存存储。所以实际上，对于CPU而言，他们是伴随程序一生的。因此仍然是全局的，只是一些全局变量必须**只能由特定的代码访问到**（这里才是static的发力点）

​	也就是说，凡是是**可访问的全局变量和函数**，那必然是伴随程序一生，需要被安排到程序的可执行文件中，占据一定的空间的（这也是为什么我说只有讨论全局变量和函数才是有意义的）。其余的内容跟我们的问题完全不相干。笔者这里写了一个程序：

```c
// demo.c
int un_g_initialized_var;
int g_initialized_var = 1;

extern int extern_var;

static int un_init_local_var;
static int init_local_var = 1;

static int local_func() {
	return 1;
}

int func() {
	return 2;
}

extern int extern_func();

int main() {
	return extern_var + extern_func();
}
```

| 符号 (Symbol)          | 类别 (Category) | 存储类别 (Storage Class)     | 链接性 (Linkage)      | 上CPU后运行时所在的内存区域 (Typical Segment) | 作用 (Function)                                        |
| ---------------------- | --------------- | ---------------------------- | --------------------- | --------------------------------------------- | ------------------------------------------------------ |
| `un_g_initialized_var` | 变量定义        | **全局** (`static` duration) | **外部** (`External`) | **BSS** (Block Started by Symbol)             | 未初始化的全局变量，运行时初始化为 0。                 |
| `g_initialized_var`    | 变量定义        | **全局** (`static` duration) | **外部** (`External`) | **Data** (Initialized Data)                   | 已初始化的全局变量。                                   |
| `extern_var`           | 变量声明        | N/A (引用)                   | **外部** (`External`) | N/A (期望在其他文件定义)                      | 引用其他编译单元中定义的全局变量。                     |
| `un_init_local_var`    | 变量定义        | **全局** (`static` duration) | **内部** (`Internal`) | **BSS**                                       | 具有文件作用域的静态变量，未初始化，运行时初始化为 0。 |
| `init_local_var`       | 变量定义        | **全局** (`static` duration) | **内部** (`Internal`) | **Data**                                      | 具有文件作用域的静态变量，已初始化。                   |
| `local_func`           | 函数定义        | **函数**                     | **内部** (`Internal`) | **Code** (.text)                              | 静态函数，只能在当前文件内被调用。                     |
| `func`                 | 函数定义        | **函数**                     | **外部** (`External`) | **Code** (.text)                              | 普通函数，可供其他文件调用。                           |
| `extern_func`          | 函数声明        | **函数**                     | **外部** (`External`) | N/A (期望在其他文件定义)                      | 引用其他编译单元中定义的函数。                         |

您思考一下上面的表格，如果您发现由任何感到费解的地方，可以自行的搜索表格理解。

## C编译器怎么看我们的文件

​	让C语言编译器行动起来，注意您编译的指令必须是

```
gcc -c demo.c -o demo.o # 欸，注意可不要掉-c，标识只编译
```

​	编译器安静的编译了一会，就把我们想要的demo.o给我们了。那编译器在编译整个单元的C文件的时候在做什么呢？

​	不管您在使用Apple clang，还是GNU gcc还是Microsoft的MSVC，他们都是**编译器**，主要的工作如您所见，就是将 C 文件从人类能够理解的文本（史山代码除外）转换为计算机能够理解的内容。编译器会将输出结果作为目标文件。在 UNIX 平台上，这些目标文件通常带有 .o 后缀；在 Windows 平台上，它们带有 .obj 后缀。

​	有趣的时，咱们的目标文件，回扣到上面的主题，无非最后内容上生成至少以下两个部分：

- 机器代码：机器代码是计算机能看得懂的0和1组成的特定指令。
- 全局变量演化出的数据：他们对应于 C 文件中全局变量的定义（对于已初始化的全局变量，变量的初始值也必须存储在目标文件中）。

​	嗯，那问题来了，您仔细看看`extern int extern_var;`和`extern int extern_func();`，熟悉`extern`关键字的朋友立马评出来不对——嗯？你这`extern_var`和`extern_func`压根就没实现啊，编译器没有发现？

​	我告诉你的是——他知道这个事情，但是**C/C++编译型语言允许你在编译的时候只出现声明而不用出现实现！**，我必须在强调一次这个**好用但是又麻烦**的特性：**C/C++编译型语言允许你在编译的时候只出现声明而不用出现实现！**。那这个事情什么时候裁决到底下不下结论——是您有意的把这些实现放置到了别处，还是就是您自己粗心大意的漏掉了实现呢？答案是下一个环节：链接。我们之后讨论，目光现在还是聚焦到编译这个环节。

## nm，好用的指令

​	Windows MSVC用户别折腾，您应该用的不是nm是dumpbin（如果，您装的是MSVC的话，我的另一个意思是您拿的Visual Studio写代码）。但是在这里，笔者就准备拿SystemV输出格式的nm来讨论了。

​	得到的可执行文件如何验证我们上面讨论的内容呢，很简单，咱们拿出来咱们的nm工具分析一下就得了。来，试一下：

```
[charliechen@Charliechen linkers]$ nm -f sysv demo.o

Symbols from demo.o:

Name                  Value           Class        Type         Size             Line  Section

extern_func         |                |   U  |            NOTYPE|                |     |*UND*
extern_var          |                |   U  |            NOTYPE|                |     |*UND*
func                |000000000000000b|   T  |              FUNC|000000000000000b|     |.text
g_initialized_var   |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data
init_local_var      |0000000000000004|   d  |            OBJECT|0000000000000004|     |.data
local_func          |0000000000000000|   t  |              FUNC|000000000000000b|     |.text
main                |0000000000000016|   T  |              FUNC|0000000000000013|     |.text
un_g_initialized_var|0000000000000000|   B  |            OBJECT|0000000000000004|     |.bss
un_init_local_var   |0000000000000004|   b  |            OBJECT|0000000000000004|     |.bss
```

​	好，让我们仔细的看看这个表格吧。你需要做的是关注一下Class这一列，他说明了咱们的这个表格是什么。

- U 类表示未定义引用，即前面提到的“空白”之一。此对象有两个类：“fn_a”和“z_global”。
- t 或 T 类表示代码定义的位置；不同的类表示该函数是本地函数 (t) 还是非本地函数 (T)——即该函数最初是否以 static 声明。同样，某些系统也可能显示一个段，例如 .text。
- d 或 D 类表示已初始化的全局变量，同样，特定类表示该变量是本地变量 (d) 还是非本地变量 (D)。如果有段，则类似于 .data。
- 对于未初始化的全局变量，如果它是静态/本地变量，则返回 b，如果不是，则返回 B 或 C。在本例中，段可能类似于 .bss 或 *COM*。



Windows的朋友，您需要打开`x86 Native Tools Command Prompt for VS Insiders`，导览到您目标的C文件后，输入`cl /c <SourceFile>.c`，这样MSVC就会只编译咱们源文件，得到的`<SourceFile>.obj`就是咱们的可重定位目标文件。这个时候，咱们可以使用dumpbin小工具：

```
dumpbin /symbols <SourceFile>.obj
```

查看符号了，笔者这里枚举一下我得到的结果（VS2026下的默认工具链）

```
D:\Windows_Programming\WindowsProgramming\demos\demos>dumpbin /symbols main.obj
Microsoft (R) COFF/PE Dumper Version 14.50.35615.0
Copyright (C) Microsoft Corporation.  All rights reserved.


Dump of file main.obj

File Type: COFF OBJECT

COFF SYMBOL TABLE
000 01048B1F ABS    notype       Static       | @comp.id
001 80010191 ABS    notype       Static       | @feat.00
002 00000003 ABS    notype       Static       | @vol.md
003 00000000 SECT1  notype       Static       | .drectve
 Section length   2F, #relocs    0, #linenums    0, checksum        0
005 00000000 SECT2  notype       Static       | .debug$S
 Section length   90, #relocs    0, #linenums    0, checksum        0
007 00000004 UNDEF  notype       External     | _un_g_initialized_var
008 00000000 SECT3  notype       Static       | .data
 Section length    4, #relocs    0, #linenums    0, checksum B8BC6765
00A 00000000 SECT3  notype       External     | _g_initialized_var
00B 00000000 SECT4  notype       Static       | .text$mn
 Section length   20, #relocs    2, #linenums    0, checksum EBBC6B4A
00D 00000000 SECT4  notype ()    External     | _func
00E 00000000 UNDEF  notype ()    External     | _extern_func
00F 00000010 SECT4  notype ()    External     | _main
010 00000000 UNDEF  notype       External     | _extern_var
011 00000000 SECT5  notype       Static       | .chks64
 Section length   28, #relocs    0, #linenums    0, checksum        0

String Table Size = 0x46 bytes

Summary

       28 .chks64
        4 .data
       90 .debug$S
       2F .drectve
       20 .text$mn
```

我们踢开其他乱七八糟的输出，实际上就是下表：

| `dumpbin` 输出                                      | 意义                      | 类比 Linux `nm` |
| --------------------------------------------------- | ------------------------- | --------------- |
| `SECT4  notype () External | _func`                 | 定义在 .text 中的外部函数 |                 |
| `SECT3  notype External    | _g_initialized_var`    | 定义在 .data 中的外部变量 |                 |
| `UNDEF  notype External    | _extern_func`          | 未定义外部函数引用        |                 |
| `UNDEF  notype External    | _extern_var`           | 未定义外部变量引用        |                 |
| `UNDEF  notype External    | _un_g_initialized_var` | 未定义外部变量引用        |                 |

## 解决我们不知道的符号：链接

​	现在我们进一步推进话题。这一步，就是解决我们在《C编译器怎么看我们的文件》这个小节抛下的问题。我们假设，在其他的文件中真的定义了这个外部的这些符号：

```c
// demo_extern.c
int extern_var = 10;
int extern_func() {
	return 3;
}
```

​	这些符号我们同样的也会编译成可重定位的目标文件。那么剩下的，就是将这些参杂了各种定义符号和未定义的符号之间，组合起来，**解决每一个文件中符号不确切的（只有名称的），定义不知晓的部分**（咱们的编译器编译通过了这些源代码文件，说明我们是声明了这些符号，但是尚未找到定义）。**这就是链接的时候我们要做的事情。**

​	现在，我们把demo_extern.c编译成demo_extern.o后，利用这个来完成我们可执行文件的最后一步：

```
gcc demo_extern.o demo.o -o demo_exe
```

​	编译当然顺利通过。这毫无疑问。

```
charliechen@Charliechen linkers]$ nm -f sysv demo_exe


Symbols from demo_exe:

Name                  Value           Class        Type         Size             Line  Section

__bss_start         |000000000000401c|   B  |            NOTYPE|                |     |.bss
__cxa_finalize@GLIBC_2.2.5|                |   w  |              FUNC|                |     |*UND*
__data_start        |0000000000004000|   D  |            NOTYPE|                |     |.data
data_start          |0000000000004000|   W  |            NOTYPE|                |     |.data
__dso_handle        |0000000000004008|   D  |            OBJECT|                |     |.data
_DYNAMIC            |0000000000003e20|   d  |            OBJECT|                |     |.dynamic
_edata              |000000000000401c|   D  |            NOTYPE|                |     |.data
_end                |0000000000004028|   B  |            NOTYPE|                |     |.bss
extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data
_fini               |0000000000001150|   T  |              FUNC|                |     |.fini
func                |000000000000112f|   T  |              FUNC|000000000000000b|     |.text
g_initialized_var   |0000000000004014|   D  |            OBJECT|0000000000000004|     |.data
_GLOBAL_OFFSET_TABLE_|0000000000003fe8|   d  |            OBJECT|                |     |.got.plt
__gmon_start__      |                |   w  |            NOTYPE|                |     |*UND*
__GNU_EH_FRAME_HDR  |0000000000002004|   r  |            NOTYPE|                |     |.eh_frame_hdr
_init               |0000000000001000|   T  |              FUNC|                |     |.init
init_local_var      |0000000000004018|   d  |            OBJECT|0000000000000004|     |.data
_IO_stdin_used      |0000000000002000|   R  |            OBJECT|0000000000000004|     |.rodata
_ITM_deregisterTMCloneTable|                |   w  |            NOTYPE|                |     |*UND*
_ITM_registerTMCloneTable|                |   w  |            NOTYPE|                |     |*UND*
__libc_start_main@GLIBC_2.34|                |   U  |              FUNC|                |     |*UND*
local_func          |0000000000001124|   t  |              FUNC|000000000000000b|     |.text
main                |000000000000113a|   T  |              FUNC|0000000000000013|     |.text
_start              |0000000000001020|   T  |              FUNC|0000000000000026|     |.text
__TMC_END__         |0000000000004020|   D  |            OBJECT|                |     |.data
un_g_initialized_var|0000000000004020|   B  |            OBJECT|0000000000000004|     |.bss
un_init_local_var   |0000000000004024|   b  |            OBJECT|0000000000000004|     |.bss
[charliechen@Charliechen linkers]$ 
```

​	现在我们看看，表变得非常的复杂，但是没关系，里面我们重点关心的是：

```
extern_func         |0000000000001119|   T  |              FUNC|000000000000000b|     |.text
extern_var          |0000000000004010|   D  |            OBJECT|0000000000000004|     |.data
```

​	我们现在终于找到了我们关心的内容了，他们现在不再是不确定的UNDEF了，而是确定定义的函数和全局变量。咱们完全可以试一试去掉extern_func的实现。

```
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
collect2: error: ld returned 1 exit status
```

​	我们熟悉的错误出现了！`undefined reference`，说明连接器向我们抱怨了他找不到`extern_func`的定义。我们仔细看看：

```
[charliechen@Charliechen linkers]$ nm -f sysv demo_extern.o
Symbols from demo_extern.o:

Name                  Value           Class        Type         Size             Line  Section

extern_var          |0000000000000000|   D  |            OBJECT|0000000000000004|     |.data
```

​	您可以看到，demo_extern解决的是extern_var的定义，但是`extern_func`的定义没找到，咱们又只给了这两个文件，自然连接器不知道上哪找到你的`extern_func`，自然也就会爆这个错误。

​	我们现在知晓了链接器的重要功能——解决最小可执行文件（为什么是最小的呢？我们之后继续讨论）的符号未定义问题。任何那些**你没提供对应信息告知定义的具体内容（那些用了的函数的源代码漏写）**的链接都会失败！最后当链接器搜寻一圈后，只要存在未定义符号（也就是nm或者dumpbin中Class是U的符号），链接器就会拉起报错：告诉你所有那些没有定义的符号。**这个时候你的解决方案非常简单——找到这些符号的可重定位文件（一般构建系统的源代码文件名和可重定位文件名相同，只有后缀不同），然后链接的时候提供！**这是所有无动态库的编译场景下解决`undefined reference`的**唯一办法**。

​	现在我们看了nm的输出，就可以回答整个问题了：

- 问1：编译器工具链是如何搜集到和查找到符号的？如何进一步的转化成更加好处理的形式
- 答：答案是编译器编译符号到计算机可以看懂的指令，将**函数符号映射成一个地址**。对于全局变量，则是将一个全局变量映射到data段中具体的访问位置。
- 问2：**我们写的变量和函数到底对计算机而言意味着什么？**
- 答：只是将我们的地址跟我们具体含义的变量关联起来，您起什么名字都无所谓。经过编译器和链接器的处理，到计算机那里只剩下一串地址了——你问我那是什么，我不到啊！问nm去！



## 额外的话题：咱们要是重复定义了呢？

​	上一节提到，如果链接器找不到符号的定义来将其与对该符号的引用连接起来，就会给出错误消息。那么，如果在链接时某个符号有两个定义，会发生什么情况呢？

​	我不急着说答案，您先动手试试看。比如说，恢复demo_extern中对`extern_func`的定义，同时，立马这样修改咱们的`demo.c`

```c
int un_g_initialized_var;
int g_initialized_var = 1;

extern int extern_var;

static int un_init_local_var;
static int init_local_var = 1;

static int local_func() {
	return 1;
}

int extern_func() { // 拷贝一份定义到这里，return您随意，因为就不影响我们的结论
	return 3;
}

int func() {
	return 2;
}

// extern int extern_func(); <- 注释掉外部查找的强调关键字extern

int main() {
	return extern_var + extern_func();
}
```

​	我们重复上面的单独编译和链接动作。很快，我们得到了另一种您可能常见的错误：

```
[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
/usr/sbin/ld: demo.o: in function `extern_func':
demo.c:(.text+0xb): multiple definition of `extern_func'; demo_extern.o:demo_extern.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

​	您注意到了，还是一样，因为编译器相信**链接器可以正确的处理任何符号的关系**（他只能一分一分的编译文件！他管不了全局其他的源文件！**整个结果单元（包含可执行文件，动态库和静态库）的符号裁决由链接器决定！**这是笔者要再强调一次的！）

​	所以，链接的时候，链接器发现两个文件中居然存在一模一样的符号定义。自然，定义是不一样，就像您即说A是1，又说A是2，唯一性被打破，贸然决定只会让程序变得不可控。所以，链接器自然一巴掌闪回来，不予通过！至少在今天的GNU工具链的默认行为下，您这样做智慧得到一个`multiple definition`。

## 那链接器的作用就这样？

​	我都这样问了，怎么可能就这样是不是？不知道您看到我反复强调这句话的时候，您有没有感受：

- 为什么是：**C/C++编译型语言允许你在编译的时候只出现声明而不用出现实现！**为什么不要求立马知道呢？好麻烦啊。

​	您冷静想一下，举个例子。我让您去邮局送一个邮件，您显然不会打断我：“闭嘴伙计，您先把邮局扛过来我看到邮件了我在帮你送”，比起来，您更加会在脑子里绘制出假象的邮局，“嗯很，我需要去一个叫邮局的地方帮忙送一个邮件”。您自然回去其他地方寻找邮件。这就是一样的道理。我们空余出来悬而未决的符号，我们自己管理和承诺他们都会出现在对应的地方——**这是您的责任而不是编译器的责任**。那好，我们现在就可以继续我们的疑问：

- 那么，除了提供源代码以外，我们是不是还能提供其他样式的信息呢？

​	欸！您的观察非常出色。如果您仔细看了看我的这段操作。

```
[charliechen@Charliechen linkers]$ gcc -c demo_extern.c -o demo_extern.o
[charliechen@Charliechen linkers]$ gcc -c demo.c -o demo.o
[charliechen@Charliechen linkers]$ gcc demo_extern.o demo.o -o demo_exe
```

​	您有没有发现，我们链接的那个步骤，好像跟源文件就没关系？毕竟咱们检索未定义符号是从可重定位文件（*.o）找的，那么，我们可不可以早就准备好一系列的可重定位文件和一组符号的声明文件，然后我们编程的时候就不用重复造轮子了，直接在**编程的时候利用这些声明文件告知编译器我担保这些符号存在**，编译的时候**通过编译生成咱们自己的可重定位文件**，然后**链接的时候把这些早就准备好的可重定位文件和我们自己的重定位文件组合起来构成一个可执行文件**呢？

​	恭喜你！你重新发明了库和接口编程的概念！您现在知晓了头文件是做什么的了吧！他就是一组符号的声明文件！那这些成千上万的可重定位文件，也别零散着，咱们**集合起来做一个库**，怎么样？当然可以！您这就是发明了历史上**大名鼎鼎的静态库**了。有些激动，但是我需要重新整理一下我们提出的概念：

- 头文件：也就是符号声明文件，**放置了我们担保符号存在的符号声明**
- 静态库：这些符号（全部或者是部分，剩下没有裁决的符号可能依赖其他的库，有趣吧！）具体的定义

​	所以我的意思是——链接器还能链接库。我可没说静态库哈，还有动态库呢。咱们先说静态库。

## 静态库：我们的符号图书馆

​	我们可以使用AR（Linux或者是Unix系统上）或者是Lib工具集合所有的可重定位文件生成静态库。

> 快速的说说细节：
>
> - 在 **UNIX** 系统上，用于生成静态库的命令通常是 **`ar`**，生成出的库文件通常带有 **`.a`** 扩展名。这些库文件通常还以 **"lib"** 作为前缀，并在传递给链接器时使用 **`"-l"`** 选项，后接库的名称（不带前缀和扩展名）。例如，**`"-lfred"`** 就会选择 **`libfred.a`** 文件。（历史上，静态库还需要一个名为 **`ranlib`** 的程序来在库的开头构建一个符号索引。如今，**`ar`** 工具通常会自行完成这项工作。）
> - 在 **Windows** 系统上，静态库具有 **`.LIB`** 扩展名，并由 **`LIB`** 工具生成。但这可能会引起混淆，因为**“导入库”（import library）**也使用相同的扩展名，导入库仅包含一个 DLL 中可用内容的列表

​	对于链接阶段，当我们提供给链接器一个静态库，整个时候，我们的链接器会持有一个尚未裁决的符号表格，沉浸到静态库中，把这些符号一个一个找出来（举个例子，A符号丢失，他在Obj1.o中，这个时候我们就会把Obj1.o全部链接进来），直到我们解决了所有的符号未定义的问题。

​	请注意从库中提取内容的**粒度**：如果需要某个特定符号的定义，则包含该符号定义的**整个目标文件**都会被包含进来。这意味着这个过程可能是“前进一步，后退一步”——新加入的目标文件可能会解析一个未定义引用，但它很可能也会带来一整套自己的新未定义引用，留待链接器去解析。

​	[`Beginner's Guide to Linkers`](https://www.lurklurk.org/linkers/linkers.html)存在一个非常出色的例子，笔者放在下面，您阅读一下：

假设我们有以下目标文件，并且链接行包含了 **`a.o`**、**`b.o`**、**`-lx`** 和 **`-ly`**。

| 文件           | **a.o**    | **b.o** | **libx.a**                             | **liby.a**                   |
| -------------- | ---------- | ------- | -------------------------------------- | ---------------------------- |
| **对象**       | a.o        | b.o     | x1.o, x2.o, x3.o                       | y1.o, y2.o, y3.o             |
| **定义**       | a1, a2, a3 | b1, b2  | x11, x12, x13; x21, x22, x23; x31, x32 | y11, y12; y21, y22; y31, y32 |
| **未定义引用** | b2, x12    | a3, y22 | x23, y12; y11; y21                     | x31                          |

1. **处理 `a.o` 和 `b.o`：**
   - 链接器将解析对 `b2` 和 `a3` 的引用。
   - 此时，未定义的引用剩下 **`x12`** 和 **`y22`**。
2. **处理 `libx.a`：**
   - 链接器检查第一个库 `libx.a`，发现可以拉入 **`x1.o`** 来满足 `x12` 引用。
   - 然而，拉入 `x1.o` 也带来了新的未定义引用 `x23` 和 `y12`。（未定义列表现在是：`y22`、`x23` 和 `y12`）。
   - 链接器仍在处理 `libx.a`，因此 `x23` 引用很容易通过拉入 **`x2.o`** 来满足。
   - 但这也给未定义列表增加了 `y11`。（未定义列表现在是：`y22`、`y12` 和 `y11`）。
   - `libx.a` 中没有其他目标文件可以解析这些剩余的符号，链接器继续处理 `liby.a`。
3. **处理 `liby.a`：**
   - 类似的流程，链接器将拉入 **`y1.o`** 和 **`y2.o`**。
   - 拉入 `y1.o` 增加了一个对 `y21` 的引用，但由于 `y2.o` 无论如何都要被拉入，这个引用很容易得到解析。
   - 最终结果是：所有未定义的引用都已解析，库中的部分目标文件（而非全部）被包含到最终的可执行文件中。

#### 链接顺序的重要性

请注意，如果（例如）`b.o` 还有一个对 `y32` 的引用，情况就会有所不同。

- `libx.a` 的链接工作方式将保持不变。
- 在处理 `liby.a` 时，链接器也会拉入 **`y3.o`** 来解析 `y32`。
- 拉入 `y3.o` 会为未解析符号列表添加 **`x31`**。
- 此时链接器已经**完成**了对 `libx.a` 的处理，因此无法找到该符号（在 `x3.o` 中）的定义，从而导致**链接失败**。这个例子清晰地说明了链接顺序（`libx.a` 在 `liby.a` 之前）的重要性。也就是说，链接器不会走回头路，您链接的时候，就必须清晰的划分编程符号所在的依赖必须是层层递进的依赖而不是循环依赖，不要给自己找麻烦！

## 动态库/共享库

​	当然，您还是现在简单的理解成动态库，最严肃的说，两者稍微存在一点区别，但是导论中，一下子讲这么严格只会把人吓跑。

​	动态库的存在更多是为了解决静态库的一个明显的缺点——每个可执行程序都拥有相同代码的副本。如果每个可执行文件都包含 printf 和 fopen 等函数的副本，这会占用大量不必要的磁盘空间。

> 您可以做一个有趣的实验，静态链接C库，看看有多大，具体的指令请您自行查找，笔者的结果是几百个MB。

​	当然，您说——我有钱，SSD随便加，这个不是最严重的问题，最严重的是——如果提供方的代码出现了bug，您完蛋了——所有的代码全部被写死进了可执行文件，您完全无法使用这个可执行文件——直到别人等了几个月编译好了才能给你！

​	为了解决这些麻烦的问题，共享库/动态库出现了（通常以 .so 扩展名表示，在 Windows 计算机上为 .dll，在 Mac OS X 上为 .dylib）。这个时候，链接器会采取一种“欠条”的方式，并将欠条的支付推迟到程序实际运行的时刻。归根结底，就是：如果链接器发现某个符号的定义存在于共享库中，它就不会在最终的可执行文件中包含该符号的定义。相反，链接器会在可执行文件中记录符号的名称以及它应该来自哪个库。

​	程序运行时，操作系统会安排这些剩余的链接工作“及时”完成，以便程序运行。在主函数运行之前，一个较小版本的链接器（通常称为 ld.so）会检查这些“借据”，并立即完成链接的最后阶段——拉入库代码并将所有代码连接起来。这意味着所有可执行文件都没有 printf 代码的副本。如果有新的、已修复的 printf 版本可用，只需更改 libc.so 即可将其插入——下次任何程序运行时，它都会被选中。

​	共享库的工作方式与静态库相比还有另一个重大区别，这体现在链接的粒度上。如果从特定共享库中提取特定符号（例如 libc.so 中的 printf），则整个共享库都会映射到程序的地址空间中。这与静态库的行为截然不同，静态库中只有包含未定义符号的特定对象才会被提取。

​	共享库咱们先只聊这么多，笔者手头有一个小300页的《高级C/C++编译技术》是专门讨论动态库/共享库技术的。足以说明这个议题是多么的复杂。我们放到后面的博客中仔细的聊。导论的话，就这样打住。

## 其他的议题：C++呢？

#### C++ 的名称修饰 (Name Mangling)

回到这个usage.cpp中：

```cpp
// in usage usage.cpp
#include <iostream>

int int_max(int a, int b); // declarations requires for usage

int main() {
	int a = 1, b = 2;
	std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
}
```

当您在 **`usage.cpp`** 这个 C++ 文件中使用 `int_max(int a, int b)` 函数时，C++ 编译器（`g++`）不会像 C 编译器那样简单地将函数名映射为 `int_max`。为了支持**函数重载**、**命名空间**、**类成员函数**等 C 语言没有的特性，C++ 编译器会对源代码中的函数名进行复杂的编码，这一过程称为**名称修饰（Name Mangling）**。

```
int int_max(int a, int b);
```

`g++` 编译器在生成 **`usage.o`** 目标文件时，会期望链接器能找到一个被修饰过的符号，例如在 GCC/Linux 环境下，它可能会查找类似 **`_Z7int_maxii`** 这样的符号（具体修饰结果因编译器和平台而异，但**肯定不是**简单的 `int_max`）。

#### C 语言库的符号名称 (Symbol Name)

可问题在于，静态库 **`libutils.a`** 是由 **C 编译器**（通常是 `gcc` 或 `cc`）编译 **`lib.c`** 文件生成的。C 编译器**不进行名称修饰**。因此，在 **`libutils.a`** 中，`int_max` 函数的符号名称就是简单的 **`int_max`**（或者加上一个下划线前缀，如 `_int_max`）。

​	您马上就知道了下面的问题了

```
g++ usage.cpp -L. -lutils -o usage
```

1. **`g++`** 编译 `usage.cpp`，生成 `usage.o`，其中包含一个对**修饰后名称**（例如 `_Z7int_maxii`）的**未定义引用**。
2. 链接器 (`ld`) 开始工作，它在 `usage.o` 中查找 `int_max`，但只找到了对 `_Z7int_maxii` 的需求。
3. 链接器在 **`libutils.a`** 中查找 `_Z7int_maxii`，但库中存在的符号是 **`int_max`**。
4. 链接器找不到匹配的符号，因此报出错误：`undefined reference to 'int_max(int, int)'`（注意：错误信息显示的是 C++ 风格的函数签名，但链接器实际查找的是其修饰后的版本）。

#### 解决方案：使用 `extern "C"`

要解决这个问题，您需要告诉 C++ 编译器：**“嘿，这个函数是用 C 编译器编译的，不要对它的名称进行修饰！”**您只需要在 C++ 文件的**函数声明**周围使用 **`extern "C"`** 链接指示符即可：

```cpp
// in usage usage.cpp

#include <iostream>

// 使用 extern "C" 告诉 C++ 编译器，这个函数的符号名要按照 C 语言的方式处理
// 即不进行名称修饰，直接查找 'int_max'
extern "C" int int_max(int a, int b);

int main() {
    int a = 1, b = 2;
    std::cout << "max in (" << a << ", " << b << "): " << int_max(a, b) << "\n";
    return 0; // 补充返回语句
}
```

重新编译并链接，程序就会成功运行，因为此时 `usage.o` 中引用的符号将是简单的 `int_max`，与 `libutils.a` 中提供的符号相匹配。