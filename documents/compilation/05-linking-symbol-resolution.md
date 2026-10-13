---
chapter: 13
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "链接器:符号解析与 undefined reference"
description: "链接器手里的两件活:符号解析把每个 U 符号对上唯一定义,重定位按 .rela 的记录把空位填掉。逐行读 undefined reference 与 multiple definition 的报错,和 readelf -r 的 Offset 一一对上,再看弱符号为什么允许强弱共存、static 为什么不进全局裁决,连忘了 main 时报错为什么指向 _start 也一并看清。"
cpp_standard: [11, 14, 17, 20]
---

# 链接器:符号解析与 undefined reference

上一篇的结尾,咱们把两处空位连同填法登记在案,把填的人也约好了:链接器。demo.o 的解剖图您已经拿到手里,今天咱们就凑到它跟前,把它的活从头看到尾。它手里的活,笔者给归成了两件。一件是符号解析(symbol resolution):每个 .o 里挂着 U 的名字,都得在别处对上唯一的定义,对不上的,链接器报的就是 undefined reference,同名的好几份都到齐了,报出来的就是 multiple definition。另一件是重定位:名字全配成对了,再按 .rela 里的记录把空位填掉。两件事的顺序是定死的:得等名字都定了,链接器才算得了地址。

照例交代环境:笔者的实验都在 WSL2 里跑,工具链还是 GCC 16.2.1 配 binutils 2.47 的组合,这一篇里的每一段输出,都是真实跑出来的。您那边的版本不同,报错的措辞、路径之类的细节可能有出入,机制本身倒是多年没变。

上一篇结尾咱们还立过一个约定:demo.o 您留着别删,今天咱们让它接着当原料,笔者说到做到。开场的头一件事,咱们请它单独链一次:

```text
$ gcc demo.o -o demo_app
/usr/bin/ld: demo.o: warning: relocation against `extern_var' in read-only section `.text'
/usr/bin/ld: demo.o: in function `main':
demo.c:(.text+0x1b): undefined reference to `extern_func'
/usr/bin/ld: demo.c:(.text+0x21): undefined reference to `extern_var'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

链接失败了,报的正是 extern_func 和 extern_var,两条悬着的引用。extern_func 的报错落在 .text+0x1b,extern_var 的报错落在 .text+0x21,拿去和上一篇里 demo.o 的重定位表对一对,Offset 列对出来的正是 0x1b 和 0x21,连类型的分工都对上了:0x1b 挂的是 PLT32,管的是函数调用,0x21 挂的是 PC32,管的是数据引用,那篇里讲过的分工原样复现。倒数第二行的 DT_TEXTREL 警告,是 extern_var 的引用落在只读段引出来的,咱们一句带过。编译器登记过的位置,链接器找不到能填的地址,就按原来的位置报告。拿它排错的门道,咱们到 a.o 身上再细讲。

不过 demo.o 只能演失败的一面:extern_var 和 extern_func 的定义,咱们手里没有文件能提供,链接成功的样子,它是演不出来的。咱们还想把每条规则都看到,所以换一对最小标本,一边是引用的文件,另一边是定义的文件,成败的两条路它都走得通。代码还是 C 写的,编译器那篇里咱们确认过,C 这边是不做名字修饰的,咱们读符号能省一道解码:

```c
// a.c —— 只声明,引用别人的符号
extern int shared_seed;
extern int helper(void);
int main(void) { return shared_seed + helper(); }
```

```c
// b.c —— 定义者
int shared_seed = 10;
int helper(void) { return 32; }
```

a.c 是引用方的本尊:shared_seed 和 helper 各有一句 extern 声明,定义落在别的文件里,main 偏偏两个都用。咱们把 a.c 编成 a.o,两个名字在符号表里都成了 U,同一对名字到了 b.o 里,一个落成了 D,另一个落成了 T。链接器要办的事,从咱们手头的两张符号表就能说全:咱们把两边的名字摆到一起,合成的就是链接器眼里的全局符号表,给每个 U 找到它的定义。

## 符号解析:把每个 U 对上唯一定义

咱们照上一篇的路子编出 a.o 和 b.o,交给链接器的命令长这样:

```text
$ gcc a.o b.o -o ok_app && ./ok_app; echo $?
42
```

链接通过了,程序也跑起来了,echo $? 打出 42。这个数是 main 交回来的:shared_seed 的 10,加 helper() 的 32。数字倒是平常,来历却要咱们停一下:a.o 里挂 U 的名字,在 b.o 里都找到了定义,配对就完成了,这正是符号解析办成事的样子。链接器报给您的大多数错,翻译过来都是这一步没办成的。没办成的样子有两种,咱们两头都看一看:定义找不到的算一头,定义太多的算另一头。

## undefined reference:报错怎么读

找不到定义的这一头,咱们把 b.o 扣下,链接器手里只剩下 a.o 了:

```text
$ gcc a.o -o fail_app
/usr/bin/ld: a.o: warning: relocation against `shared_seed' in read-only section `.text'
/usr/bin/ld: a.o: in function `main':
a.c:(.text+0x5): undefined reference to `helper'
/usr/bin/ld: a.c:(.text+0xb): undefined reference to `shared_seed'
/usr/bin/ld: warning: creating DT_TEXTREL in a PIE
collect2: error: ld returned 1 exit status
```

咱们由上往下读。头一行是随行的一条警告,说有一处冲着 shared_seed 的重定位落在只读的 .text 里,咱们把它放一边,今天的正题不在它身上。第二行是位置的开场白,指出下面的引用出自 a.o 的 main。中间那两行才是真正的报错:helper 的定义没找到,shared_seed 的定义也没找到,链接器把它俩全报了。末尾的两行,DT_TEXTREL 警告加 collect2 的收尾,和开场 demo.o 的输出一模一样,咱们就不逐行念了。

开场咱们拿 demo.o 把报错和重定位表对过一遍,这个读法您记牢,咱们拿 a.o 再走一遍。a.o 的两条报错,helper 的落在 .text+0x5,shared_seed 的落在 .text+0xb,您拿 readelf -r 看 a.o,Offset 列里对出来的正是 0x5 和 0xb。坐标的含义和刚才一样:偏移 0x5 的那 4 个字节,该填的数要从 helper 的地址算出来,可它翻遍了手里的目标文件,也没有找到谁定义了 helper,开场看到的情形,到这儿又是一样的。

您能把报错和重定位表对上,这是本卷里顶实用的排错技能,往后您再遇上链接报错也不怕,拿它对一遍就有数了。排查的动作分两步。头一步拿 nm 把相关的 .o 过一遍,看这个名字是哪几个文件里的 U,确认引用是从哪儿发出的。第二步要问的是定义从哪儿来:是不是有个 .o 忘了挂上链接命令,还是定义住在一个没写的库里。咱们把库的问题留给下一篇和库搜索那篇。咱们今天这对标本,答案其实是明摆着的:命令行里缺的就是 b.o。

## multiple definition:定义太多也不行

定义太多的另一头,笔者再备了一份 dup.c,干的事就一件:把 helper 又定义了一遍。

```c
// dup.c —— 故意重复定义 helper
int helper(void) { return 99; }
```

```text
$ gcc a.o b.o dup.o -o dup_app
/usr/bin/ld: dup.o: in function `helper':
dup.c:(.text+0x0): multiple definition of `helper'; b.o:b.c:(.text+0x0): first defined here
collect2: error: ld returned 1 exit status
```

第二行的信息最足,一句话报了两边,咱们分着看。dup.c 这边的 helper 落了个 multiple definition,而 b.o 那边,同一个位置被标成了 first defined here。谁前谁后您都不用猜,链接命令里的 b.o 排在 dup.o 前面,链接器按见到的顺序记下了头一份。第三行又是咱们认识的 collect2,开卷那篇里咱们见过,链接器 ld 的包装,它替这场失败的链接收了尾。

链接器为什么一点不通融?咱们替 call 指令想一下就明白了:a.o 里调用 helper 的位置,目标偏偏只能填一个地址。b.o 说 helper 在我这儿,dup.o 也说在我这儿,两份都是堂堂正正的定义,选了谁,另一份的代码就没人引用了,选错了更是没法收场。所以它执行的是一条硬规则:进了全局符号表的强定义,一个名字只有一份是合法的,等第二份到了,链接器当场就拦下了。C++ 在语言层面还有一条同源的规则,咱们叫它 ODR,说的就是单一定义规则,编译器那篇里咱们点过名,完整的展开留给模板与 ODR 那篇。有个细节咱们得说说:dup.c 自己编译的时候,它是半个字都不报的。咱们前面就看明白了,编译器只看得见眼前的翻译单元,b.c 和 dup.c 互相不知道对方的存在,谁跟谁重了名,只能等所有的 .o 在链接器这儿碰了面才裁决得了。

## 弱符号:强弱共存,强的赢

上一节的规则听着挺硬,其实还留了一个例外。上一篇的字母表底下,咱们还欠着 W 和 V 没介绍,约好了遇到了再认识。今天到场的是 W。

```c
// weak_def.c
__attribute__((weak)) int policy(void) { return 1; }
```

```c
// strong_def.c
int policy(void) { return 2; }
```

```c
// use_policy.c —— 调用方
#include <stdio.h>
int policy(void);
int main(void) { printf("policy=%d\n", policy()); return 0; }
```

weak_def.c 里的那行 `__attribute__((weak))`,您可能是头一回见。ISO C++ 的标准里没有它,它出自 GNU 家的扩展写法,GCC 和 LLVM 家的 Clang 都是认它的,作用是把 policy 的定义标成弱符号。强弱是一对相对的称呼:咱们平时写的定义什么都不挂,落到的身份全是强符号,标了 weak 的定义,身份就降成了弱的。咱们把三份 .o 链到一起,看它们各自的符号:

```text
$ gcc use_policy.o weak_def.o strong_def.o -o poly_app && ./poly_app
policy=2
$ nm weak_def.o | grep policy
0000000000000000 W policy
$ nm strong_def.o | grep policy
0000000000000000 T policy
```

跑出来的是 policy=2,强定义赢了。nm 的两行输出把身份写得明明白白:weak_def.o 里的 policy 是大写 W,弱符号的记号,strong_def.o 里还是咱们的老熟人 T。字母表里 W 管的是弱的函数,V 管的是弱的数据对象,今天出场的是 W,V 您混个脸熟就行。

规则咱们一句话就能说完:一个名字的强定义,再配上几份弱的定义,写法是合法的,链接器选中的是强的。要是全是弱的,链接器就选命令行里靠前的一份。您对照上一节品一品这个反差:两份强定义碰了面,当场报的就是 multiple definition,强的配上弱的,反而能相安无事地共存。这个例外对应的是一类正经的需求:库想给某个名字备一份默认的实现版本,又想留着被使用者顶替的余地,弱符号干的正是这个活。glibc 里就有它的身影:pthread 老接口的那批兼容桩(pthread 是 Linux 的线程库),还有性能剖析用的启动钩子 `__gmon_start__`,背后用的都是这一套机制。具体到每一种的用法,咱们不在这儿展开,您留个印象就行:链接器的手里,除了硬规则之外,还多了一套强弱裁决。

对咱们写 C++ 的人来说,弱符号还有一层更要紧的身份。编译器那篇里咱们存过一个问题:头文件被粘贴进了十个翻译单元,bigger<int> 就实例化出了十份同名的代码,链接的时候怎么不打架?答案的方向就在这儿。内联函数和模板的实例,编译器给它们的正是一份份弱定义,再配上一种叫 COMDAT 的机制:它是一种节分组的机制,同组的同名节,链接的时候只保留一份,重复的直接丢掉。十份 bigger<int> 就这么合成了一份,完整的展开,咱们留给模板与 ODR 那篇。

## static:不进全局裁决的名字

说完了强弱,还有一类名字是压根不进这场裁决的。咱们看两个同名变量:

```c
// s1.c
static int counter = 1;
int get1(void) { return counter; }
```

```c
// s2.c
static int counter = 2;
int get2(void) { return counter; }
```

```c
// s_main.c —— 调用方
#include <stdio.h>
int get1(void);
int get2(void);
int main(void) { printf("%d %d\n", get1(), get2()); return 0; }
```

两个文件里各写了一个 counter,而且写的都是定义。按前头 multiple definition 那里立下的硬规则,这不该拦下吗?咱们跑跑看:

```text
$ gcc s1.o s2.o s_main.o -o static_app && ./static_app
1 2
$ nm s1.o | grep counter
0000000000000000 d counter
$ nm s2.o | grep counter
0000000000000000 d counter
```

链接通过了,输出的 1 2 就是两个 counter 的值。咱们拿手里的 nm 输出一对:两行里的 counter 都是小写 d,上一篇的字母表说过,小写是 static 私有的记号。正式的说法叫内部链接性(internal linkage):链接性说的是一个名字能被多大范围看见,内部的,只在本身所属的翻译单元里有效。编译器那篇里咱们还遇过另一个带 linkage 的词:语言连接,它管的是名字怎么修饰,您别把这俩混成一件事。两个 counter 的名字一模一样,却都是本地符号,各自待在自家的符号表里,全局表里没有它们的身影。所以链接器没拿它们互相比过,所谓的冲突,自然也就无从谈起了。

链接器的管辖范围,到这里就清楚了:它裁决的,只是进全局符号表的名字,字母表里大写 T、D、B 的那一批。小写的私有符号,static 函数和 static 的全局变量,是不归它管的。边界在日常写代码的时候也用得上:您在头文件里放一个辅助函数,又担心多个文件包含之后重了名,给它挂上 static 就可以了,C++ 里同款的做法是匿名 namespace,它就成了每个文件私有的一份,名字自然也就冲突不起来了。

## 连 main 都不特殊

最后咱们看一个您八成真遇上过的报错。咱们这回链接 b.o:它的家底您都认识,shared_seed 和 helper 都见过了,唯独缺了 main。

```text
$ gcc b.o -o no_main
/usr/bin/ld: /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o: in function `_start':
(.text+0x1b): undefined reference to `main'
collect2: error: ld returned 1 exit status
```

第一行的长路径,是 Scrt1.o 在笔者机器上的真实住址。Scrt1.o 您在开卷第一篇里见过,是 collect2 每次链接都默认捎上的启动文件之一。它的里面住着 _start,程序的真正入口,上一篇里咱们看可执行文件的 Entry 地址时点过它的名。_start 的代码要调用 main,于是在 Scrt1.o 的符号表里,main 就是一条待解析的 U。b.o 里偏偏没有它,链接器照章办了事,报出来的,就是 _start 对 main 的 undefined reference,和 a.o 报的两条是同一种错。

咱们细看,这场报错里有两处耐人寻味的地方。头一处的报错,主角不是咱们的代码:引用是从启动文件里发出的,报错里挂的路径是 Scrt1.o 的,函数名是 _start 的,偏移 0x1b 指的也是 Scrt1.o 自己的 .text,连报的位置都是它自己的,源文件名更是没处挂的,启动文件是编译器套装里早就备好的,没有咱们的源码可指。另一处的看点,是链接器对谁都一视同仁:main 的名字在 C++ 里再怎么特殊,走到符号解析这一步也就是一条普通的 U,没有定义也是照样报的。您哪天手滑把 main 拼成了 mian,编译器是一声不吭的,轮到链接的时候,链接器替您接住。

_start 在 main 前头都干了什么,启动文件的那一排各管什么,这些归程序启动的那篇专门讲。今天咱们只需要带走一件事:连程序的入口,走的都是符号解析。

## 重定位落地:U 变成 T

等符号解析全都通过了,链接器才做它的第二件事:重定位。上一篇里咱们把记录的每一列都读过了,算式咱们也亲手写过。算术的活链接器代劳了,咱们来验收结果:

```text
$ nm a.o | grep helper
                 U helper
$ nm ok_app | grep helper
000000000000112c T helper
```

咱们看 helper 的身份变化:链接之前的它,在 a.o 里是一个连地址都没有的 U。链接之后的它,成了带地址的 T。链接器把 b.o 的定义合进了 ok_app,给 helper 排定的位置就是 0x112c。pie 的身份,咱们在开卷那篇里见过。它的意思是,helper 的 0x112c 是链接期定下来的,程序加载的时候还会整体挪动,背后的机制,咱们到位置无关那篇再看。a.o 里 main 发出的 call,此刻也按上一篇的式子填好了。空位到底填上了没有,您不用信笔者的话,拿 nm 一查就知道了:符号解析和重定位两件事的结果,都落在最终的符号表里。

## 从一堆 .o 到库的想法

咱们回头看看今天敲过的链接命令。命令 gcc a.o b.o -o ok_app 的眼里只有 .o,连源码的影子都见不到。b.o 是咱们刚编出来的,还是三个月前就编好躺在硬盘上的,命令是一个字都不用改的。这个性质往下想一步就有意思了:像 b.o 这样管定义的一批 .o,咱们能不能提前把它们编好,整整齐齐地收进一个文件,要用的时候整份地交给链接器,省得咱们每次从源码重来?这个想法的成品就是库,静态库是打头的一种。打包的具体做法,还有链接器面对一整包 .o 时怎么取用,咱们统统留给下一篇。

## CMake 视角

今天咱们手工拼的链接命令,里头的 .o 一只手就数得过来了。到了真实的工程里,一条链接命令动辄就是几百个 .o 外加一排库的规模,这么长的名单谁来拼?拼名单的活,归的是构建系统。上一篇的 CMake 视角里,咱们让 OBJECT 库 demo_core 挂上过一句 target_link_libraries,今天正好把它的含义补全:

```cmake
target_link_libraries(app PRIVATE demo_core)
```

它表达的是链接期的依赖:链接 app 的时候,要把 demo_core 的产物一起带上。app 是被链接的目标,demo_core 是带上的依赖,PRIVATE 您在预处理那篇和上一篇里都见过了,它把依赖锁在了 app 自己身上,下游的目标沾不着光。几百个目标谁在前谁在后的顺序,CMake 按依赖关系替咱们排好,拼成一条 collect2 认识的命令。

构建系统拼命令是在行的,读报错它就帮不上忙了。哪天您用 CMake 构建到链接这一步,undefined reference 刷了一屏,它也只能原样转给您。那时候您用的还是今天的读法:拿 nm 查一查名字是谁的 U,再确认一下定义的来源,看看是不是漏挂了哪个目标。命令是 CMake 拼的,裁决是链接器做的,报错也就得用链接器的语言来读。

## 走完编译这一半

咱们从开卷第一篇里 g++ hello.cpp -o hello 那一行出发,把整条路一段一段亲手跑过了:四万四千九百七十一行的 hello.i(预处理那篇),mangle.cpp 的符号串和按需生成的 bigger(编译器那篇),demo.o 的节表、符号表和两条重定位的记录(上一篇),今天又看着链接器把 a.o 和 b.o 配了对、填了空位,合出一个报 42 的 ok_app。从一行源码到一个能跑的程序,每一层的真实样子,您都亲手见过、亲手查过,不再是听来的。

编译的这一半走完了,下面的一半,换的是另一个问题:咱们的代码要怎么复用。复用的第一步是把一堆 .o 打包成静态库,下一篇开篇讲的就是它。至于咱们每天链接的 libstdc++、glibc 凭什么能被数不清的程序共用,咱们把答案留给动态库总览那篇。今天的这几个 .o,您留着再跑一遍也行,咱们下一篇见。
