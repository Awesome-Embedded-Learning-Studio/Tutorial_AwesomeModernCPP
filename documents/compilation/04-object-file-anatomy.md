---
chapter: 13
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- intermediate
- 工具链
title: "目标文件解剖:.o 里面有什么"
description: "用 readelf -h 与 -S 看 ELF 头和节表,nm 把九个符号逐个对回 demo.c,readelf -r 逐列读重定位记录与 -4 的来历,再看 -g 加出的 DWARF 调试节、size 的三列合计,以及 Windows 上 COFF 与 dumpbin 的对照。"
cpp_standard: [11, 14, 17, 20]
---

# 目标文件解剖:.o 里面有什么

上一篇的结尾咱们说好了:带上工具,今天咱们把 .o 打开。要看的疑问也记着仨:它的内里怎么排布,重定位的记录长什么样,.text 和 .data 里装的又是什么。开卷那篇里还有一句更早的预告:hello.o 那行 not stripped 是天大的好消息,解剖目标文件全指着的正是符号表。今天咱们就把这些话一笔一笔兑现。

不过主角要换人。mangle.cpp 的源码里一个全局变量都没有,.data 和 .bss 就没了内容可看。hello.cpp 又太瘦了,六行代码撑不起咱们这场解剖。所以笔者另备了一份 demo.c:全局的、静态的、初始化的、没初始化的、只声明不定义的,各种情形它全占了一份。它还有一层方便:是 C 写的,上一篇里咱们看过,C 这边免了名字修饰这道工序,符号表里躺着的全是本名,咱们读输出能省一道解码。

咱们照例交代环境:笔者的实验都在 WSL2 里跑,工具链用的是 GCC 16.2.1 配 binutils 2.47,这一篇里的每一段输出,都是真实跑出来的。您那边版本不同,节的数目、字节的多少这类细节可能有出入,布局本身倒是多年没变。

## 三件工具,各管一段

上一篇结尾说要带的 objdump,咱们请上桌,顺便把它两位同门也一起认全了。binutils 家里干这一行的,常用的就这么三件:

- nm,上一篇里跟咱们共过事:它读符号表,目标文件里有哪些名字、每个名字什么身份,它一页报给您。
- readelf,今天头一回出场:它专读 ELF 的结构,文件头、节表、符号表、重定位表,都能原样摆给您看。
- objdump,几位里资格最老:它最拿手的绝活是反汇编,加个 -d,就把 .text 里的机器码一条条翻回汇编给您读。

三件的活有重叠,节表的清单,readelf 和 objdump 都能给您列。咱们今天要看的是结构与符号,对口的就是 readelf 和 nm。objdump 的反汇编,留到要对着机器码抠细节的场合再请它。

## 解剖对象:demo.c

咱们这就上代码,您边看边数,写下的全局名字一共有九个:

```c
int un_g_initialized_var;
int g_initialized_var = 1;
extern int extern_var;
static int un_init_local_var;
static int init_local_var = 1;
static int local_func() { return 1; }
int func() { return 2; }
extern int extern_func();
int main() { return extern_var + extern_func(); }
```

咱们把九个名字分四组点一遍。func 和 main 是有定义的全局函数。local_func 也定义了,但挂了 static,生效的范围只在本翻译单元里。g_initialized_var 和 init_local_var 是给了初值的变量,一个是全局的,另一个是 static 的。un_g_initialized_var 声明了却没给值,un_init_local_var 的初值同样空着。extern_var 和 extern_func 手里有的只是一句 extern 声明,它们的定义压根不在这个文件里,main 还偏偏用了它们俩。

咱们编一下,产物就是今天的解剖对象:

```bash
gcc -c demo.c -o demo.o
```

九个名字各自落到什么地方,咱们拿工具逐个看过去。

## ELF 头:REL,一份待定的身份

readelf -h 的输出有十几行,咱们挑最要紧的四行:

```text
  Class:                             ELF64
  Type:                              REL (Relocatable file)
  Machine:                           Advanced Micro Devices X86-64
  Entry point address:               0x0
```

Class 说它是 64 位的 ELF,Machine 说它面向的处理器是 x86-64。Type 这一行写的是 REL,它的全称是 Relocatable file、可重定位文件。开卷第一篇里 file 命令报过的那个 relocatable,官方的写法就是它。这个身份意味着什么,咱们看 Entry 那行再确认一遍:入口地址写的是 0x0。可执行文件的入口指向程序的第一条指令。开卷那篇里编出来的 hello_app 是 pie 程序,您拿 readelf -h 看它,Type 是 DYN(固定地址的可执行文件则是 EXEC),Entry 指向一个叫 `_start` 的位置。那是运行库的起点,main 之前的事归它管,咱们留到程序启动那篇再展开。而 .o 这边,地址一个都还没定,咱们连“从哪儿开始执行”这样的问题都无从谈起,所以这里只能是 0。这样的待定是分工的产物:定地址这件事,整个编译阶段都没有做的打算,全留给了链接器。

## 节表:.o 里的每一块地方

ELF 把文件划成了一块一块,每一块都有自己的名字和用途,这样的块就叫节(section)。所有节被 readelf -S 列成了一张表,完整的输出里每个节还有 Size、Flag 好几列,笔者裁掉了,省略号是笔者加的,留下这几列是咱们接下来要用的:

```text
  [Nr] Name              Type             Address           Offset
  [ 0]                   NULL             0000000000000000  00000000
  [ 1] .text             PROGBITS         0000000000000000  0000000040
  [ 2] .rela.text        RELA             0000000000000000  000000002b0
  [ 3] .data             PROGBITS         0000000000000000  000000006c
  [ 4] .bss              NOBITS           0000000000000000  0000000074
  [ 5] .comment          PROGBITS         ...
  [ 6] .note.GNU-stack   PROGBITS         ...
  [ 7] .note.gnu.pr[...] NOTE             0000000000000000  000000000090
  [ 8] .eh_frame         PROGBITS         ...
  [ 9] .rela.eh_frame    RELA             ...
  [10] .symtab           SYMTAB           ...
  [11] .strtab           STRTAB           ...
  [12] .shstrtab         STRTAB           ...
```

列的含义咱们认一遍:[Nr] 是序号,Name 是节的名字,Type 是这块字节的身份,Address 是节在最终地址空间里的位置,Offset 是它在文件里的位置。您扫一眼 Address 那列,清一色的 0。ELF 头里的待定身份,落到每一节的头上还是待定,将来的住处谁也不知道。Offset 倒是有真值,比如 .text 的字节从文件 0x40 处开始。接下来咱们一块一块看这些节里都住了谁。

.text 的类型是 PROGBITS,意思是这块装着货真价实的字节。咱们写的机器码全落在这里:local_func、func、main 三个函数翻完的字节,一个接一个地排开。

.data 的类型也是 PROGBITS,放的是初始化过的全局与静态变量。咱们在 demo.c 里给的初值 1,这会儿就以字节的形式躺在这里。

.bss 的类型是 NOBITS,这一行请您多看一眼:NOBITS 宣布的事就一件,这个节在文件里占的字节数为零。没有初值的 un_g_initialized_var 和 un_init_local_var,存它们的只是一份大小记录,程序启动的时候,内存里划出这块地方并清成零,字节是运行时才有的。它省的是什么,您细想一下就能明白:咱们要是声明一个 1MB 的未初始化数组,难不成真在文件里铺 1MB 个零?咱们还能从 Offset 列把这件事验出来:.data 的起点是 0x6c,占的长度是 8 个字节,加出来的终点正好是 0x74。.bss 的 Offset 也写着 0x74,可它的类型是 NOBITS,文件里并没有它的字节,这个数字只是记录里排定的位置。

.rela.text 的类型是 RELA,装的是重定位记录,咱们留一整节专门伺候它。

.symtab 和 .strtab 是一对搭档:SYMTAB 的意思就是符号表,STRTAB 的意思就是字符串表。符号的名字没有直接嵌在符号表里,而是集中收在 .strtab,符号表里记的是一个指过去的偏移。同样的机制还有一份 .shstrtab,您猜它装的是谁的字符串?节名自己的。咱们眼前这十几个节名,收留它们的正是 .shstrtab。

杂项咱们也数得出三件。.comment 放编译器的版本注记,.note.GNU-stack 是一条跟栈有关的注记,[7] 那个 .note.gnu.pr[...] 记的是 demo.c 用到的指令集属性标记,三件都不进咱们的正题。倒是 [7] 的名字得多说一句:它名字里的 [...] 跟笔者裁列用的省略号不是一回事,那是 readelf 在默认列宽下截长名字的记号,加个 -W 就能看到它的全名:.note.gnu.property。

.eh_frame 存的是栈展开用的表,异常处理、栈回溯指望的都是它。咱们在 demo.c 里连一个异常都没写,这个节照样留在了文件里,连同给它配的 .rela.eh_frame,咱们今天只认个脸,细节就不铺开了。

## 符号表:九个名字逐个对回源码

这就轮到 nm 出场了,您拿它对照着 demo.c 读,咱们一条都别放过:

```text
$ nm demo.o
                 U extern_func
                 U extern_var
000000000000000b T func
0000000000000000 D g_initialized_var
0000000000000004 d init_local_var
0000000000000000 t local_func
0000000000000016 T main
0000000000000000 B un_g_initialized_var
0000000000000004 b un_init_local_var
```

nm 按名字排了序,左列给的是地址,中间的那一列是字母,右列给的是符号名,九个名字一个不落地全在上面。咱们一组一组对回去。

头两行连地址都没有,字母是清一色的 U。U 的意思是 undefined、未定义:extern_var 和 extern_func 在这个文件里只有一句声明,定义住在别的文件里。咱们在开卷那篇推过,undefined reference 的报错出自链接那一程。而哪些名字还悬着,在 .o 这一程就已经写定了,nm 给咱们看得清清楚楚。

咱们看字母的大小写:func 和 main 拿的是大写 T,local_func 拿的是小写 t。大 T 报的是三样信息:定义在本文件、内容在代码段、外部可见,别的目标文件链接时都能引用它。小写 t 是同一件事的私有版,挂了 static 的函数只属于本翻译单元。咱们再把三个地址一对:local_func 的地址是 0x0,func 的是 0xb,main 的是 0x16,这就是哥仨在 .text 里的排位,而且全是节内偏移,和节表里 Address 清一色的 0 是同一份待定。

咱们接着对变量:g_initialized_var 的字母是 D,init_local_var 的字母是 d。D 对的是 .data:初始化过的变量,字节真的在文件里。您把地址也一对:一个记的是 0x0,另一个记的是 0x4,两个 int 一前一后地排着,正好把 .data 的 8 个字节分掉了。

un_g_initialized_var 的字母是 B,un_init_local_var 的字母是 b,对的是 .bss。地址还是 0x0 和 0x4 的老搭配,不过这里的地址只是编号而已,字节却是要等运行时才有的,底细咱们在讲节表时已经看过了。

您发现规律没有?这套字母表其实就回答两个问题。头一个问的是符号住在哪:T 是代码、D 是已初始化、B 是未初始化,字母取的就是各节名字的打头。一个连住处都没有的符号,拿到的就是 U。第二个问的是公有还是私有:大写是公有,小写的是 static 私有。您拿两条规律回看 demo.c,每个 static 都落成了小写。咱们把规律收成一张表,您以后随查随用:

| 字母 | 含义 | demo.c 里的例子 |
|------|------|-----------------|
| U | 只引用,定义不在本文件 | extern_var、extern_func |
| T / t | 代码,公有 / static 私有 | func、main / local_func |
| D / d | 已初始化数据,公有 / static 私有 | g_initialized_var / init_local_var |
| B / b | 未初始化数据,公有 / static 私有 | un_g_initialized_var / un_init_local_var |

> 字母表还有别的成员,比如弱符号的 W 和 V,咱们这次的输出里没露面,后面遇到它们时咱们再认识。

本卷往后咱们每一篇都得拿它说话:想看清一个库该导出的东西齐不齐,想追一个找不到定义的符号,翻开工具的头一眼都是它。

## 重定位表:两处空位与它的填法

咱们讲节表时按下没讲的 .rela.text,现在轮到它了。上一篇讲汇编器 as 的时候留了一句话:碰上本文件没有的定义,as 就把指令里的地址空着,再补一条重定位(relocation)的记录,记录的意思是:这里的地址还没定,咱们等链接器来填。记录集中的地方就是眼前这个节:

```text
$ readelf -r demo.o
Relocation section '.rela.text' at offset 0x2b0 contains 2 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
00000000001b  000a00000004 R_X86_64_PLT32    0000000000000000 extern_func - 4
000000000021  000b00000002 R_X86_64_PC32     0000000000000000 extern_var - 4
```

头一行的 0x2b0,和咱们在节表里看到的 .rela.text 的 Offset 对得上,说的就是同一个节。输出往下其实还有一小段,是 .rela.eh_frame 的三条记录,伺候 .eh_frame 里那些表用的,那个节咱们只认了个脸,这里咱们同样略过不贴。咱们把眼前两条记录的来历想清楚:main 的 return 里要调 extern_func,编译器就生成了一条 call。要读 extern_var 的时候,编译器又生成了另一条 rip 相对的数据引用。两条指令里各有一个 4 字节的偏移字段,目标地址编译器也给不了,收尾的活归 as:它把填不上的字段往 .rela.text 里登记,改的是哪个位置,找的是哪个符号,按的是什么算法。main 的起点是 0x16,两条记录的 Offset 是 0x1b 和 0x21,都落在 main 的机器码里。行尾的符号名把对应关系写得明明白白,咱们不用猜。

咱们一列一列读。Offset 指向要改写的那 4 个字节在 .text 里的位置。Info 这一列里把类型和符号编号编在了一起,readelf 已经替咱们解开了,摆在后面的 Type 和 Sym. Name 里,咱们直接看这两列。咱们再扫一眼 Sym. Value 那一列,里面清一色的 0,因为这些符号的值同样没定。真正有讲究的是 Type 和最后的 Addend,咱们分别说。

Addend 里那个 -4 的来历,咱们请出 RIP 相对寻址来解释。x86-64 的这类指令不写绝对地址,写的是一个偏移,基准是下一条指令的开头,也就是指令执行时 RIP 的值。两条指令的偏移字段,都正好是整条指令的最后 4 个字节:字段的末尾就是指令的末尾,而 RIP 恰恰落在那儿。这么一来,CPU 算距离的基准(RIP、字段末尾)和链接器填值的基准(P、字段开头)之间,隔着字段自己的 4 个字节,-4 就是把这段差补平的修正项。落到公式上的样子是这样的:PLT32 按的是 L + A - P,其中的 L 是 PLT 条目的地址。PC32 按的是 S + A - P,其中的 S 是符号本身的地址。两个式子的形状一样,算的路子都是目标加 A 再减 P,得数都是目标相对本指令的距离。两条记录的 A 都是 -4,来历就是上面说的那 4 个字节。

Type 的两个名字,咱们得分清楚,这里有个流传很广的误读。咱们从表面看起:call 上落的是 PLT32,数据引用上落的是 PC32,正好咱们一人一条。这个分工本身是真的:按 x86-64 的 psABI(processor-specific Application Binary Interface、处理器专属的二进制接口规范),以及 binutils 2.36 起的约定,汇编器给 call、jmp 这类分支指令的操作数发 PLT32,给 lea、mov 这类 rip 相对的数据引用发 PC32,看的是指令的类别。可您要是把 PLT32 读成“这个调用运行时一定经过 PLT”,那就读拧了。PLT32 的含义温和得多:它标记的是一个调用点,链接器在需要的时候可以为它建一个 PLT 条目。PLT 咱们在开卷那篇远远望过一眼,函数调用走的跳板,专门的讲解在位置无关那篇。链接器要是能确定这个符号不会被别处的定义顶替,它就不建跳板了,而是把调用直接连到函数本体,算出的结果与 PC32 没有分别。所以两个类型在运行时的计算完全同构,差别只是给链接器留没留建跳板的余地。这一段的口径,笔者按 x86-64 psABI 和 maskray 的考据文章核对过。maskray 是 LLVM(GCC 之外另一套主流的开源工具链)那边的链接器维护者,您想深挖,R_X86_64_PLT32 就是现成的关键词,一搜文章就到手了。

两处空位就这么登记在案,填法的公式也写好了。干这个活的人是谁,咱们文末见。

## 段的合计:size 与 -g 的对照

节是 .o 内部的划分,咱们还有个更高层的视角:size,它把数字按段合计了报给咱们。开卷第一篇咱们认过 text、data、bss 三个名字:text 装的是代码,data 装的是已初始化数据,bss 装的是未初始化数据,dec 和 hex 是三列的合计:

```text
$ size demo.o
   text	   data	    bss	    dec	    hex	filename
    209	      8	      8	    225	     e1	demo.o
```

现在您能拿 nm 的结果核对了:data 的 8,正是 D/d 那一对 int 的合计。bss 的 8,记的是 B/b 那一对。text 的 209 咱们也能数出来,用的正是节表里被笔者裁掉的 Size 列:.text 的机器码占 41 个字节,.note.gnu.property 占了 48,.eh_frame 占了 120,三个数都记在了 text 这一列名下,合计的正是 209。

咱们再编一份带调试信息的,看看 .o 胖在了哪里:

```text
$ gcc -g -c demo.c -o demo_g.o && readelf -S demo_g.o | grep debug
  [ 5] .debug_info       PROGBITS ...
  [ 6] .rela.debug_info  RELA ...
  [ 7] .debug_abbrev     PROGBITS ...
  [ 8] .debug_aranges    PROGBITS ...
  ...还有 .debug_line 等
```

节表里多出一排 `.debug_` 打头的节,里面装的是 DWARF。DWARF 是 ELF 世界里调试信息的格式标准,gdb 的断点、单步、看变量,读的就是这些节。名字的意思也直白,.debug_line 记的是行号,.debug_info 记的是类型和变量。那 size 胖了没有?咱们把两份放一起:

```text
$ size demo.o demo_g.o
    text data bss dec hex filename
     209     8   8 225  e1 demo.o
     209     8   8 225  e1 demo_g.o
```

三列是一模一样的。调试信息住在自己的 .debug_* 节里,程序运行时装进内存的段没有它们的位置,所以 size 一个字节没动。而 demo.c 还是同一份,您带不带 -g,运行时要装进内存的部分都没有变化。咱们想剥掉它们,有专职的工具 strip,binutils 家的又一员,把符号表和调试节一并请了出去,文件当场就瘦了一圈。开卷那篇里 file 报的那句 not stripped,咱们当时已经道过喜,现在能说全了:它保住的就是这批东西。咱们在成品发布前跑一遍 strip,也是常见的收尾。

## Windows 这边:COFF 与 dumpbin

咱们手上的工具都是 Linux 这边的,Windows 上您也有同款。Windows 那边的主编译器叫 MSVC,它的全称是 Microsoft Visual C++,编出来的目标文件后缀是 .obj,用的格式叫 COFF,它的全称是 Common Object File Format,Windows 家的目标文件格式。咱们看它不靠 nm,靠的是 dumpbin,它是 Visual Studio 自带的,查符号的子命令是 dumpbin /symbols,打出来的是 COFF SYMBOL TABLE。概念您全能对上:咱们的大写 T、D、B,在那边写成了 SECTn,意思是符号的定义落在第 n 个节里。咱们这边的 U,对应的就是 UNDEF External。节、符号表、重定位这套东西也不是 ELF 的专利,Windows 换了格式名和工具名,咱们今天练的读法照样使得。

## CMake 视角

咱们全程在终端里手工敲命令,可日常构建里的事,CMake 也都留了位置,咱们对一遍。

头一件咱们说目标文件本身。add_library 不写类型的时候默认是 STATIC,编译出的 .o 会被 ar 打包成 .a,打包的细节归静态库那篇讲。要是您想让 .o 保持原样直接交给链接,CMake 就备了这么一档类型:OBJECT

```cmake
add_library(demo_core OBJECT demo.c)
```

它编出来的只有目标文件,打包和出库的活它都不干。咱们在预处理和编译器那两篇里攒工程,用的都是那个 app。您要让 app 用上它,一句 target_link_libraries(app PRIVATE demo_core) 就挂上了,这些 .o 就直接进了 app 的链接。咱们手头有一批 .o 要被好几个目标共用、又不想为它们单独立一个静态库时,它正好合咱们的用。

第二件咱们拿工具看产物。今天的三件工具想在构建后自动跑一遍,路径不用您手抄,CMake 备了生成器表达式:$<TARGET_FILE:app> 在配置阶段还是一串字面量,构建时才展开成 app 这个产物的完整路径。您可以用它给 app 接一条收尾命令:

```cmake
add_custom_command(TARGET app POST_BUILD COMMAND nm $<TARGET_FILE:app>)
```

构建完成后它就替咱们对产物自动跑一遍 nm,输出就落在构建日志里了,咱们翻日志就能看到符号。挂的位置也有讲究:它得挂在 app 这样真正的二进制目标上,demo_core 那样的 OBJECT 库是挂不上的,CMake 在配置阶段就直接报错拒收了。

第三件咱们说 -g。您想要 demo_g.o 那样的产物,不用您自己往编译命令里塞 -g:上一篇里列过 CMAKE_BUILD_TYPE 的对应表,Debug 档默认带的是 -g,RelWithDebInfo 给的是 -O2 加 -g。那篇里提醒过的事这儿同样适用:您压根不设置构建类型,塞进命令的 -g 就一个都没有,到手的还是素的目标文件。

## 链接器接手

咱们要的材料都见过了:ELF 头给了 demo.o 一份待定的身份,节表交代了每一块字节,符号表列了九个名字,重定位表记下了两处空位,连填法都写好了。接下来该干活的人上场:链接器按什么顺序消化这些记录,九个名字里挂着的两个 U 它去哪儿找定义,找不到时报的 undefined reference 是怎么生成的,两个文件重了名它又怎么裁决。这些问题的答案都在下一篇里,它的篇名,写的是《链接器:符号解析与 undefined reference》。demo.o 您留着别删,那篇咱们还拿它当原料。
