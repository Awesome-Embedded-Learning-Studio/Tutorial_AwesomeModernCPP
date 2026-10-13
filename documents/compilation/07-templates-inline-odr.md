---
chapter: 13
difficulty: intermediate
order: 7
platform: host
reading_time_minutes: 17
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 模板
title: "模板、inline 与 ODR:十份翻译单元,一份实体"
description: "十个翻译单元各包含同一个模板头,nm 里各出一份弱符号,链接后合成一份实体。展开 ODR 允许多处定义的条件,实测复现同名 inline 两副定义时链接器静默挑一份的未定义行为,再看 inline 的真实语义、extern template 把生成收拢到一处的前后对拍,以及 CMake 的 INTERFACE 库怎么表达纯头文件的家底。"
cpp_standard: [11, 14, 17, 20]
---

# 模板、inline 与 ODR:十份翻译单元,一份实体

编译器那篇讲 bigger 的一节收尾,咱们把一个问题存到了今天:bigger 的定义跟着头文件走,十个翻译单元都调了 bigger<int>,十份一模一样的机器码就要在各家 .o 里各躺一份,链接的时候它们怎么不打架?解开它,靠的是一条语言层面的规定:One Definition Rule,咱们叫它 ODR,翻过来的名字叫单一定义规则。预处理那篇里咱们点过它的名,正式的展开说好了留给今天。

咱们还得把一个误会排掉:十份 bigger<int> 不是出了什么事故,它是 C++ 特意允许的局面。语言里有明文的规定,有一类的定义可以出现在多个翻译单元里,开出的条件也只有一条,就是各处的定义必须长成同一个。今天咱们把规定展开,再顺着工具链的实际做法走一遍:编译器给这样的符号落什么身份,而链接器拿什么把十份合成一份。最后咱们还要故意违反一次条件,看看会发生什么,那一幕比 multiple definition 的当场报错安静得多。

照例交代环境:笔者的实验都在 WSL2 里跑,工具链还是 GCC 16.2.1 配 binutils 2.47 的组合,CMake 用的是 4.4.3,这一篇里的每一段输出,都是真实跑出来的。您那边的版本不同,符号、耗时之类的细节可能有出入,机制本身倒是多年没变。

## ODR:从严到宽的三档要求

ODR 咱们不陌生了,今天轮到咱们把它正式展开。它管的事情,咱们按从严到宽分成三档说。头一档管的是单个翻译单元,在单元的内部,同一个名字的定义顶多一份,执法的是编译器:预处理那篇里没有守卫的 no_guard.h 被包含两次,struct Point 被看见了两回,当场就报了 redefinition,链接连出场的机会都没有了。第二档管的是整个程序,普通的函数和全局变量,一个名字的定义全程序只有一份。这一档的执法者是链接器,链接器那篇里两份 helper 碰出来的 multiple definition,就是它的手笔。

第三档走的是放宽。类定义、inline 函数、模板连同它们的实例、constexpr 函数,都在放宽的名单里:咱们可以在每个翻译单元里各放一份定义,这样写是合规的。这个放宽您其实早就在用了:预处理那篇的头文件守卫挡住的是同一个单元里的第二次包含,真到了多个单元各含一份的场景,struct Point 的定义就在几个单元里各有一份,照样编得好好的,靠的正是第三档的放宽,编译器那篇的 bigger 走的也是同一条路。

放宽不是无条件的,条件就写在标准的 [basic.def.odr] 一节里,咱们挨个看过去。文本这头说的是,各处的定义要由完全相同的记号序列构成,记号(token)是编译器切分源码的原子单位,像变量名、括号、分号这样的东西,而注释和空白都进不了清单,所以两份定义的注释写得不一样,咱们不用管它。名字这头说的是,定义里出现的名字,查找的结果要落到同一个实体上。位置这头说的是,定义所在的类和命名空间也得对得上。三头都满足了,多处的定义才算同一个实体的多份拷贝。可但凡差了一个记号,像多了一个分号、换了一个常量、被条件编译剪掉了一块,它就不再是同一个定义了,程序也随之落入了未定义行为(undefined behavior),标准从此不再对这个程序的行为做任何承诺。这句话的分量,咱们今天用真实的实验量出来。

## 定义为什么躲不开头文件

您可能有个直觉的想法:既然多处定义带来了这么多麻烦,那就把模板的定义收进一个 .cpp,别的单元只给声明,全程序不就只剩一份定义了?咱们直接拿实验检验。笔者备了三份材料:一个只有声明的 bigger_decl.h,一个自带定义并显式实例化了 bigger<int> 的 provider.cpp,一个只包含声明头、调了 bigger(3, 4) 的 consumer.cpp,外加一个带 main 的 decl_main.cpp。等它们编成了 .o,咱们看两边的符号:

```text
$ nm consumer.o | grep bigger
                 U _Z6biggerIiET_S0_S0_
$ nm provider.o | grep bigger
0000000000000000 W _Z6biggerIiET_S0_S0_
```

consumer 的眼里没有函数体,编译器那篇里咱们确认过,编译器对编不出来的模板实例无能为力,只好挂一个 U 了事,而 provider 手里握着的是一份 W。U 碰上了 W,居然也配成了,程序也真跑起来了:

```text
$ g++ provider.o consumer.o decl_main.o -o decl_app && ./decl_app
2 4
```

可这样的路子,咱们走不远。笔者又写了一个 consumer2.cpp,它只调 bigger 的 double 版本,而 provider 没替 double 实例化过:

```text
$ g++ -c consumer2.cpp && g++ provider.o consumer.o consumer2.o decl_main.o -o decl_app2
/usr/bin/ld: consumer2.o: in function `consumer2_calc()':
consumer2.cpp:(.text+0x1d): undefined reference to `double bigger<double>(double, double)'
collect2: error: ld returned 1 exit status
```

链接当场就断了。所以收进单个 .cpp 的路子,它的代价高得吓人:您得在一个 .cpp 里手工维护一张全工程用到的类型组合清单,清单但凡漏了一行,链接就跟着断了一次,而工程一大,咱们就更没谱了。模板的定义必须进头文件,问题的根子就在这里:用到它的单元看得见定义,生成的活儿也归它自己。预处理那篇讲宏的一节收尾,咱们留过一句话,说真要写的是短小逻辑,比宏更体面的选择是 inline 函数和模板,今天可以把话兑现了:它们的定义进头文件,是语言正式许可的写法,而不是预处理器玩的把戏。标准库满头文件的 std::sort 和 std::vector,靠的也是同一条规则。

## 编译器落下记号:弱符号与 COMDAT

十个翻译单元里的十份 bigger<int>,凭什么让链接器认出是同一个实体?靠的是身份标记。这个身份您在链接器那篇里见过:弱符号,nm 输出里的 W。咱们把编译器那篇的 bigger 原样搬进头文件,配上十个同款的翻译单元:

```cpp
// bigger.h(外面包着预处理那篇讲过的头文件守卫)
template <typename T>
T bigger(T a, T b) { return a > b ? a : b; }
```

十个单元是同款的,tu1.cpp 里的 calc1 把 bigger 的 int 版和 double 版各调了一次,而 tu2.cpp 到 tu10.cpp 只有编号不同。main.cpp 把 calc1 到 calc10 各调了一遍,把返回值的合计打印出来。等全部编成了 .o,咱们看两家的符号表:

```text
$ nm tu1.o
0000000000000000 T _Z5calc1v
0000000000000000 W _Z6biggerIdET_S0_S0_
0000000000000000 W _Z6biggerIiET_S0_S0_
```

calc 是咱们手写的普通函数,拿的是强符号 T。bigger 的两个实例,拿的身份清一色是 W,tu2.o 到 tu10.o 躺着的是同样的两个 W,十个 .o 里的 bigger<int> 恰好凑成十份,和预计的一样。GCC 把这类允许多处定义、链接时合成一份的实体统称为 vague linkage,直译过来的名字叫模糊链接性。ISO 的标准里是查不到这个词的,它是 Itanium C++ ABI 里的称呼,就是 GCC 在 Linux 上遵循的那套 C++ 二进制接口规范,编译器那篇讲名字修饰时咱们见过它。您以后翻 GCC 的文档和链接器文章,还会一而再地遇到它。

W 只是符号表里的身份,真正把合并落实的,是链接器那篇里说过同组同名只留一份的 COMDAT。咱们今天自己看一回它的样子:

```text
$ readelf -g tu1.o

COMDAT group section [    1] `.group' [_Z6biggerIiET_S0_S0_] contains 1 sections:
   [Index]    Name
   [    7]   .text._Z6biggerIiET_S0_S0_
```

紧接着的第二组一模一样,组名换成 bigger<double> 的修饰名,咱们裁去了。每个模板实例没有混进大块的 .text,而是自己占一个专属节,外面套着一个用符号修饰名命名的组。您把组的名字当成链接器判重的依据就行:两个组同名,就是同一个实体的两份拷贝。

## 十份进,一份出

咱们把十个 .o 和 main 链到一起:

```text
$ g++ tu1.o tu2.o tu3.o tu4.o tu5.o tu6.o tu7.o tu8.o tu9.o tu10.o main.o -o ten_app && ./ten_app
sum=55
```

程序正常跑起来了,咱们把 calc1 到 calc10 的返回值加在一起,得出的数正是 55。再看可执行文件里的符号:

```text
$ nm ten_app | grep bigger
00000000000011b3 W _Z6biggerIdET_S0_S0_
0000000000001197 W _Z6biggerIiET_S0_S0_
```

进链接的是十份 bigger<int>,出来的只剩一份,bigger<double> 走的也是一样的路。留下的两行身份还是 W,活下来的一份是从弱定义里挑出来的,弱符号的性质跟着进了最终的程序。咱们再问一句,组呢?

```text
$ readelf -g ten_app

There are no section groups in this file.
```

没有了。等合并完成了,组也就地解散了,专属的节也被并进了最终的 .text,咱们拿 readelf 在可执行文件里翻,连一个组都找不到了。链接器干的事,咱们一句话就说得全了:见到没见过的组名就收下,再见到同名的组就整组丢弃,留下来的,是链接命令里排在前面的一份。有一件事咱们得说明白:十份定义逐记号相同,留谁丢谁的差别看不出来,程序的运行结果都一样。留下排在前面的一份,也只是 GNU ld 眼下的行为,规范并没有白纸黑字的承诺。而且链接器从头到尾没有比对过十份的内容,连比对的动作都没有。定义是不是真的相同,它是不管的,负责的是写程序的人。那万一不相同呢?这正是咱们接下来要亲手做的实验。

## 两副定义,一个名字:静默的未定义行为

接下来是全篇分量最重的一段,咱们故意违反一回 ODR,看看会发生什么。做法很直接:咱们开两个翻译单元,各给同名的 inline 函数一副不同的定义。

```cpp
// side_a.cpp
inline int answer() { return 10; }
int from_a() { return answer(); }

// side_b.cpp
inline int answer() { return 20; }
int from_b() { return answer(); }
```

odr_main.cpp 把 from_a 和 from_b 都打印了出来。编译的时候,咱们把 -Wall 和 -Wextra 全都打开了,得到的却是一片安静,一个字的警告都没有。链接器那篇里咱们确认过,链接前的编译器只看得见眼前的翻译单元,side_a 和 side_b 互相不知道对方的存在。两份 .o 里 answer 的身份也对称,咱们 grep 一看,两边各躺着一个一模一样的 W。

咱们把它链起来,两回一起跑给您看:一回用原样的顺序,一回用颠倒的顺序。

```text
$ g++ odr_main.o side_a.o side_b.o -o odr_app && ./odr_app
from_a=10 from_b=10
$ g++ odr_main.o side_b.o side_a.o -o odr_app2 && ./odr_app2
from_a=20 from_b=20
```

咱们两轮都跑完了,事情就清楚了:程序安安稳稳地跑了起来,answer 处处返回的是几,全看链接命令里谁排在了前面。最值得多看一眼的,是头一轮的 from_b=10。side_b.cpp 的源码里,return 20 就写在 from_b 的旁边,跑出来的却是 10。咱们看 side_b.o 里的重定位,就能知道为什么:

```text
$ readelf -r side_b.o | grep answer
000000000005  000400000004 R_X86_64_PLT32    0000000000000000 _Z6answerv - 4
000000000020  000300000002 R_X86_64_PC32     0000000000000000 .text._Z6answerv + 0
```

from_b 里的调用,落的是头一条冲着符号 `_Z6answerv` 的重定位。第二条挂在 .rela.eh_frame 的名下,是栈展开表指回函数开头用的,咱们不用管它。编译期它认的只是一个名字,本文件里的定义住在哪里,它是不关心的。链接的时候,side_b.o 里的 answer 作为重复组被丢弃了,而重定位最终填进去的,是幸存者 side_a 的地址。所谓我自己文件里的函数,等到了链接这一步,它手里剩下的只有一个名字,谁活下来了就指向谁。

咱们把口径说严谨:这个实验演示的是违规之后发生了什么,答案是什么都没有发生,全程的安静是真实的。咱们把违规演示给您看了,可它的非法身份并没有变,标准把这样的程序判成未定义行为,它今天跑出来的每个数都不欠咱们解释。您换一个优化档、换一个链接器、再换一次链接顺序,它的表现都可能换成另一副样子。真实工程里这么出事的,写代码的人多半没这个打算,祸根是同一份头文件被剪出了两副面孔。头文件本身好好的,两个编译命令带了不同的宏,就成了下面这样:

```cpp
// flavor.h(外面包着头文件守卫)
#include <cstdio>
#ifdef NEW_STATS
inline int bucket_count() { return 16; }
#else
inline int bucket_count() { return 4; }
#endif
inline void report(int v) { std::printf("bucket=%d\n", v); }
```

咱们编 old_code.cpp 时一个宏都不带,而 new_code.cpp 的编译带上 -DNEW_STATS,链好了就跑:

```text
$ g++ old_code.o new_code.o face_main.o -o face_app && ./face_app
bucket=4
bucket=4
```

两行输出的都是 4。写 16 的那副定义明明编进了 new_code.o,却在链接时被整组丢弃了,连 new_code.cpp 自己的调用都被改绑到了 4 上,而两个 .o 里 bucket_count 的身份,咱们用 nm 看也还是同样的 W。构建系统里一处 -D 的差异,就能造出这样的程序,而且没有工具在旁边喊一声,因为从每个单元自己的视角看,谁都是无辜的。

有没有工具能抓到一部分?-flto 是链接时优化,编译器那篇里咱们提过它,专门的展开在 LTO 与链接器那篇,它让所有翻译单元在链接时见了面。类型长两副面孔的一类,配上 GCC 专报 ODR 违规的警告开关 -Wodr 就抓得到了,咱们另备了字段顺序不同的两个 struct Config 验过,警告当场就点名了。可 inline 函数长两副函数体的一类,咱们拿 side_a 和 side_b 加 -flto -Wall -Wextra 试过,这回咱们一声警告都没等到,程序照常跑出了 from_a=10 from_b=10。所以防线终究在写法上:同一个实体的定义,咱们只从同一份头文件里拿。条件编译可以剪的是实现,可别剪出同名不同体的两副面孔。

## inline 的现代语义:链接资格在前,内联提示在后

预处理那篇留下的另一半,今天咱们一并对上:inline 函数凭什么能待在头文件里。inline 关键字给函数的东西有两样。头一样给的是资格,定义可以出现在多个翻译单元里的资格。您在头文件里写下一个 inline 函数,几十个单元包含了它,于是每个单元各生成了一份弱符号、套上了一个 COMDAT 组,和模板实例一模一样的机制,咱们在前面讲弱符号和 COMDAT 的一节里全部验过。constexpr 函数和类内定义的成员函数,语言直接给它们发了隐式 inline 的待遇,原因是同款的,它们的定义生来就要进头文件。

第二样东西才是字面意思上的内联,它给的是一个建议,建议的内容是把调用改写成函数体本身。咱们拿三件套验证:

```cpp
// inl.h(外面包着头文件守卫)
inline int twice(int x) { return x * 2; }
constexpr int triple(int x) { return x * 3; }
struct Box { int v; int get() const { return v; } };  // 类内定义,隐式 inline
```

咱们在 -O0 下编一遍,三份定义全都老老实实落了符号:

```text
$ g++ -c use_inl.cpp && nm use_inl.o | grep -v U
0000000000000000 T _Z3runi
0000000000000000 W _Z5twicei
0000000000000000 W _Z6triplei
0000000000000000 W _ZNK3Box3getEv
```

run 是咱们写的调用方。标了 inline 的 twice,在汇编里照样吃了一个 call:

```text
$ g++ -S use_inl.cpp -o use_inl.s && grep -E "call.*_Z5twicei" use_inl.s
	call	_Z5twicei
```

咱们换成 -O2 再编,三个 W 连影子都没有了:

```text
$ g++ -O2 -c use_inl.cpp -o use_inl_O2.o && nm use_inl_O2.o
0000000000000000 T _Z3runi
```

您细看这组对照:inline 写了,优化的开关没拉,三份函数体咱们一个都没看到被内联,原样生成了。而反过来的情形也是真的:没标 inline 的普通函数,优化器想铺开的时候照样铺开,裁决权从头到尾都在编译器的手里。所以现代 C++ 里 inline 的定位很清楚,它是为定义进头文件服务的,不是求编译器把我铺开。

## extern template:把到处生成收拢成一地生成

十份 W 在链接时是合成了一份,可编译的时候,十份还是照样各生成了各的,每个翻译单元都得给自己的 bigger<double> 实例化一遍。模板小的时候,这点重复劳动也就不算什么了,而模板一大,劳动量就按翻译单元的数目翻倍。C++11 给了一个收拢的写法,写法的名字叫 extern template,正式的名字是显式实例化声明(explicit instantiation declaration),意思用一句话说就够了:这个实体别在我这儿生成,别处已经有了。

咱们换个分量足一点的模板做对照。heavy_median<T> 的内部是 sort、unique、reverse 三件套,定义咱们照旧放在头文件里。变体一咱们什么都不动,十个翻译单元(TU)直接把它用了,编译出的目录就叫 noext。变体二则在头文件的末尾多加了一行 `extern template double heavy_median<double>(std::vector<double>);`,另立的一个 instantiate.cpp 写下显式实例化定义,编出的目录就叫 withext。两边的符号,咱们对拍:

```text
$ nm noext/tu1.o | grep heavy_median
0000000000000000 W _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
$ nm withext/tu1.o | grep heavy_median
                 U _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
$ nm withext/instantiate.o | grep heavy_median
0000000000000000 W _Z12heavy_medianIdET_St6vectorIS0_SaIS0_EE
```

变体一是咱们看过的老样子,每个 .o 都各躺了一份 W。而在变体二这边,tu1.o 对 heavy_median<double> 只剩了一个 U,编译器被那行声明拦下了,实体搬进了 instantiate.o,它的身份还是 W。您看,一地生成用的仍然是同一套弱符号机制,extern template 只是把生成的地点从到处收成指定的一处,链接期的合并规则一个字都没改。两个变体都链接成功了,输出也是一致的,而真正的差别在体积上:noext 侧的 tu1.o 是 46976 字节,而 withext 侧的 tu1.o 只有 14376 字节。

咱们把十个 TU 逐个编译,耗时咱们在 WSL2 上各测了三轮:变体一花的是 2.5 到 2.9 秒,变体二花的是 1.85 到 1.96 秒,再补上 instantiate.cpp 单独一份的 0.2 秒出头。体量大的模板,省下来的就是半秒上下,而模板越重、翻译单元越多,这个差距的放大也就越明显。

代价咱们也得说全。extern template 转走的是生成的活,而不是把活取消:十个 TU 都声明了别处有,结果谁也没管生成,链接器照样报了错:

```text
$ g++ lonely.cpp -o lonely_app
/usr/bin/ld: /tmp/ccGYaLA0.o: in function `main':
lonely.cpp:(.text+0x49): undefined reference to `double heavy_median<double>(std::vector<double, std::allocator<double> >)'
collect2: error: ld returned 1 exit status
```

它比声明加手工清单的原始做法高明在哪?定义还在头文件里。清单外的类型组合没人收拢,用到的单元照旧自己隐式实例化,断链的事也就不会发生,咱们真跑过:一个只包含 extern template 版头文件的单元用了 heavy_median<float>,自己生成了一份 W,链接也照常通过了。收与不收的边界,由那一行声明精确地圈定。而标准库自己也在用,libstdc++ 的 bits/basic_string.tcc 里写着 `extern template class basic_string<char>;`,为的就是别让每个翻译单元都实例化一份 string。还有一个新鲜的注脚:那行声明带着 `__cplusplus <= 202002L` 的条件,C++23 起 libstdc++ 把它撤了,让 string 恢复了按需生成,收益和代价都摆在明面上了,而库作者也在两头权衡。

## CMake 视角:纯头文件的库,写成 INTERFACE

咱们今天的 bigger.h 要是攒多了,就是一个纯头文件的库,全部的家底都在 include/ 里,连 .cpp 的影子都找不到。CMake 为这样的库备下了专门的目标类型:

```cmake
cmake_minimum_required(VERSION 3.26)
project(iface_demo CXX)
add_library(mathkit INTERFACE)
target_include_directories(mathkit INTERFACE mathkit/include)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE mathkit)
```

类型咱们写的是 INTERFACE,它的意思说来简单:目标自己不产出任何构建的产物,它存在的意义,是把用法要求传给链接它的目标,CMake 的术语叫使用要求(usage requirements)。落到咱们眼前的要求就一条,把 mathkit/include 加进包含的路径。mathkit.h 里放的是一个 inline 函数加一个模板,app 包含了它,也链接了它,构建和运行的结果如下:

```text
$ cmake --build build
[1/2] Building CXX object CMakeFiles/app.dir/main.cpp.o
[2/2] Linking CXX executable app
$ ./build/app
42 40
```

咱们拿工具再验收一轮。看的是构建目录里 mathkit 的名下:咱们用 find 找过去,结果一个都没有,.a、.so、.o 的影子都没见着,整个构建里唯一的链接产物就是 app。而传出去的要求落没落地,咱们接着看。app 的编译命令里,实打实多了一行指向 mathkit/include 的 -I。咱们配置工程的时候指名了 Ninja 生成器,CMake 写出的构建脚本就是 build.ninja,您翻它就能看到这行 -I。INTERFACE 和目标文件解剖那篇里见过的 OBJECT 库,正好是各站一头的格局。OBJECT 库出的是 .o,打包的活它不干,而 INTERFACE 连 .o 都没有,传出去的只有路径。STATIC 和 SHARED 站在中间的位置,产出的东西也打包成库。您写 add_library 挑类型的时候,只要把实体在哪儿生成的位置想清楚,答案就自己出来了。

## 复用的下一步:从各带一份到共用一份

上一篇把一堆 .o 打包成了静态库,谁链了它,谁的可执行文件里就多一份。今天咱们把另一半也说圆了:定义跟着头文件走进十个翻译单元,十份实体的合法性从哪里来,链接的时候又怎么把它们合成一份,链路上的每一环您都亲手验过,连违规时的安静也亲眼看过。而到这儿为止,复用的形态始终是每个程序各带一份,有了一百个用 bigger<int> 的程序,磁盘上就躺了一百份,内存里躺的也是一百份。

那 libstdc++ 和 glibc 凭什么能被数不清的程序共用?链接器那篇的结尾,咱们把这个问题存给了动态库总览那篇。答案的方向,就是让库单独地占一个文件,而程序只挂一个引用,构建期做的是登记,运行期做的是接上,两边的机制就是动态库总览那篇的正题。今天的十个 .o,您留着重跑也行,扔了重写也行,几分钟的事。
