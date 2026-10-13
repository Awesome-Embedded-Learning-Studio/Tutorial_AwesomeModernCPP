---
chapter: 13
difficulty: intermediate
order: 6
platform: host
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
- 工具链
- CMake
title: "静态库:ar 打包与 -l/-L 链接"
description: "用 ar rcs 把一摞 .o 打包成 lib<name>.a,ar t 看成员清单,nm 逐成员读符号、读出档案索引;再用 -l 与 -L 链接,拿 nm 与 size 对照成员级抽取:被 undefined reference 点名的成员整成员进可执行文件,没人点名的成员一个符号都不进。"
cpp_standard: [11, 14, 17, 20]
---

# 静态库:ar 打包与 -l/-L 链接

上一篇的结尾咱们立过一件事:像 b.o 这样管定义的一批 .o,咱们可以提前编好,整整齐齐地收进一个文件,要用的时候整份交给链接器,省去了每次从源码重来的工夫。今天咱们就来办成这件事。原料您都熟,a.o 和 b.o 是上一篇的原班,它们俩在那篇里是配过对的,一个挂着待定义的 U,另一个管的就是定义。笔者另备了两份新料:

```c
// extra.c —— 新料:一个文件里住俩函数
int bonus_a(void) { return 7; }
int bonus_b(void) { return 100; }
```

```c
// a2.c —— 调用方二号:除了 helper,还点名 bonus_a
extern int helper(void);
extern int bonus_a(void);
int main(void) { return helper() + bonus_a(); }
```

extra.c 是预备进库的第二份料,一个文件里住了 bonus_a 和 bonus_b 两个函数。a2.c 是备用的调用方二号,它点名的函数跟 a.c 不完全一样,它的用场,到后面您看得到。

环境咱们照例交代:笔者的实验都在 WSL2 里跑,工具链还是 GCC 16.2.1 配 binutils 2.47 的组合,CMake 用的是 4.4.3,这一篇里的每一段输出,都是真实跑出来的。您那边的版本不同,字节的多少这类细节可能有出入,机制本身倒是多年没变。

## ar 打包:把一摞 .o 收进一个 .a

打包用的工具叫 ar,它的全称是 archiver、归档器,binutils 家的又一位成员,目标文件解剖那篇里咱们用过的 nm、readelf 都是它的同门。它管的活只有一件:把若干文件原样收进一个叫 archive 的档案里。咱们把四个目标文件编出来,再敲一条打包的命令:

```text
$ gcc -c a.c b.c extra.c a2.c
$ ar rcs libmyutil.a b.o extra.o
$ ar t libmyutil.a
b.o
extra.o
$ file libmyutil.a
libmyutil.a: current ar archive
```

命令里的 rcs 咱们一个字母一个字母地看。打头的操作码是 r,它的意思是替换并入:名单里的文件并进档案,同名成员要是已经在了,旧的就被换掉。后面的两个是修饰符:c 的意思是档案还不存在时安静地把它建出来,小写 s 的意思是给档案写一份符号索引,索引里写的是什么,下一节咱们专门看。t 是另一个列清单用的操作码,ar t 打出来的就是成员的名字,咱们放进去的是 b.o 和 extra.o,它列出来的一个不多、一个都没有少。

file 那行报的是 current ar archive,说的就是现行格式的 ar 档案。归档文件这个概念您就按字面理解,它是一个专门装别的文件的文件。成员的完整字节原样躺在里面,外面裹的只有档案自己的管理信息——成员头、目录和索引,也就是下面要对的 244 字节。咱们拿 ls -l 对一对:b.o 的大小是 1256 字节,extra.o 的大小是 1328 字节,两份的合计是 2584,而 libmyutil.a 的大小是 2828 字节,多出来的正是那 244 字节。

这样的 .a 就是静态库(static library)。它的特点是:链接的时候,库里的代码整段并进了可执行文件,之后的运行就不再需要库文件了。跟它相对的动态库,咱们结尾再见。

## 档案的内部:成员与索引

nm 是个不挑食的,裸的 .o 和 .a 它都读,两种输出的样子却不一样,咱们对着看:

```text
$ nm b.o
0000000000000000 T helper
0000000000000000 D shared_seed
$ nm extra.o
0000000000000000 T bonus_a
00000000000000b T bonus_b
$ nm libmyutil.a

b.o:
0000000000000000 T helper
0000000000000000 D shared_seed

extra.o:
0000000000000000 T bonus_a
00000000000000b T bonus_b
```

裸 .o 的输出您在目标文件解剖那篇和上一篇里看熟了,顶头排的就是符号。对 .a 它换了一个排法:成员的名字带着冒号打头,底下缩着的就是这个成员的符号,成员之间还空了一行。档案自己是没有符号表的,符号全在各成员的身上,nm 只是把每个成员挨个读了一遍。字母表还是目标文件解剖那篇的那一套,helper 和 shared_seed 拿的还是老字母 T 和 D,bonus_a 和 bonus_b 两个新面孔拿的也是 T,四条定义也就齐了。

索引咱们专门看。咱们给 nm 带上 -s 再跑一遍,档案的头上就多出一段:

```text
$ nm -s libmyutil.a

Archive index:
shared_seed in b.o
helper in b.o
bonus_a in extra.o
bonus_b in extra.o
```

咱们看头一条,`shared_seed in b.o`,说的是符号与成员的对应:符号的名字在前,成员的名字在后。完整的 nm -s 在索引的后面还会把每个成员的符号原样再列一遍,跟上一块的输出一模一样,笔者裁掉了。这样的对应关系就是 rcs 里那个 s 写下的索引,链接器查它就不用挨个翻开成员了。写索引的活另有专职的工具:ranlib,binutils 家的又一位,它对档案干的事跟 ar s 一模一样,单独成了一条命令。有一个实情笔者得报给您:binutils 2.47 的 ar 造新档案的时候,连 rc 不带 s 都默认把索引写上了,想拿到无索引的档案,反倒要显式写大写的 S。咱们真造一对来核对:

```text
$ ar rcS noidxS.a b.o extra.o && ls -l libmyutil.a noidxS.a
-rw-r--r-- 1 charliechen charliechen 2828 Sep 30 15:10 libmyutil.a
-rw-r--r-- 1 charliechen charliechen 2712 Sep 30 15:10 noidxS.a
```

两个档案的成员一模一样,大小却差了 116 字节:带索引的 libmyutil.a 是 2828 字节,不带索引的 noidxS.a 是 2712 字节,差出来的正是索引。无索引的库在本机链接器手里也能链成,它就退化成了把成员逐个翻开自己找。索引的用处就是省掉这番翻找,咱们还是按惯例写 rcs。

## -l 与 -L:按名字要库

库里有了货,链接的时候怎么点它?咱们看命令:

```text
$ gcc a.o -L. -lmyutil -o lib_app && ./lib_app; echo $?
42
```

眼前的两个选项,咱们各认一遍。`-l` 后面跟的是库名,注意是小写的 L。链接器见着 -lmyutil 的时候,不会去找叫 myutil 的文件,它按的是约定好的拼法:名字的前面加一个 lib,后面按库的类型补后缀,静态库补的是 .a,拼出来的文件名就是 libmyutil.a。动态库的后缀 .so 也在同一套约定里,两种后缀谁排前面的讲究,咱们留给库搜索那篇。`-L` 的意思是往库的搜索路径里添一个目录,`-L.` 添的就是当前目录。

这层约定有个现成的反证。笔者把库复制一份,放进一个单独的 naming 目录,改名成了 myutil.a,lib 的前缀没了,咱们再链一次:

```text
$ gcc a.o -Lnaming -lmyutil -o naming_app
/usr/bin/ld: cannot find -lmyutil: No such file or directory
/usr/bin/ld: note to link with naming/myutil.a use -l:myutil.a or rename it to libmyutil.a
collect2: error: ld returned 1 exit status
```

找不到库的时候,链接器报的就是 cannot find。第二行的 note 更有意思,它把约定亲口说了出来:要么照着 note 说的用 `-l:myutil.a` 指名,要么把文件的名字改回 libmyutil.a。库文件的名字为什么都以 lib 打头,底细就在 note 的这一句话里:拿 lib 打头的这个名字,链接器拼名字的规则就认它,咱们看着顺不顺眼,它是不管的。不凑这个约定的时候也有办法,像 `gcc a.o ./libmyutil.a` 这样的写法,链接器也是照收的,只是日常的工程里不这么用。这套拼名的做法出自 Unix 这一系,Windows 那边同款的东西叫 .lib,MSVC 家给 ar 干活的工具叫 lib.exe,对照的事咱们到库搜索那篇和 ABI 那篇再说。至于搜索路径里目录的次序,库明明存在却报找不到的种种情形,咱们也统统留给库搜索那篇,本篇要立住的只是约定本身。

## 成员抽取:被 undefined reference 牵着走

42 出来了,链接倒是成了,可链接器手里的 .a,它是整个吞下的吗,还是另有讲究?咱们做个对照,一边走的是库,另一边链的是散装的两个 .o:

```text
$ gcc a.o -L. -lmyutil -o lib_app && ./lib_app; echo $?
42
$ gcc a.o b.o extra.o -o direct_app && ./direct_app; echo $?
42
$ nm lib_app | grep -c bonus
0
$ nm direct_app | grep bonus
0000000000001137 T bonus_a
0000000000001142 T bonus_b
$ nm lib_app | grep -E "helper|shared_seed"
000000000000112c T helper
0000000000004010 D shared_seed
```

咱们对着符号表看,两个程序都跑出了 42,符号表却是两样的天地。直链出来的 direct_app,命令行上写了几个 .o 就全量进几个,extra.o 的两个 bonus 连同没人用的字节,一个不落地并进了 direct_app。经库链出来的 lib_app 呢?bonus 的名字一个都搜不到,grep 数出来的是 0。被抽进去的只有 b.o 的老两位,helper 的 T 和 shared_seed 的 D。

章法您在上一篇里见过,用的正是符号解析。链接器手里攥着 a.o 的两条 U,shared_seed 和 helper 两条都悬而未决。它翻开档案的索引一查,这俩名字都登记在 b.o 的名下,于是只把 b.o 成员取出来并进了程序。轮到 extra.o 就没人点名了,成员整个儿地留在了原地。这就是成员级的抽取:命令行上的 .o 是全量地进,档案里的成员是被 undefined reference 牵着按需地进,这就是静态库与一摞散 .o 的本质区别。

抽取的最小单位是成员,成员的内部没有更细的挑拣。这话咱们得验,咱们换 a2.o 上场。它的 main 只点了 helper 和 bonus_a 两个名字,nm a2.o 报的 U 也确实只有这俩。链出来您看:

```text
$ gcc a2.o -L. -lmyutil -o app2 && ./app2; echo $?
39
$ nm app2 | grep bonus
0000000000001141 T bonus_a
000000000000114c T bonus_b
```

跑出来的 39,您一算就知道是 helper 的 32 加 bonus_a 的 7。可 bonus_b 的名字也在符号表里,它明明是没人引用的,照样进来了。因为点名的是 extra.o 这个成员,成员进来的时候是整个进来的。这也是工程里一条务实的经验:静态库的成员按功能拆细,一个 .c 只放一组同进同退的函数,抽取的粒度才对得上。塞一个大杂烩的 .c 进库,用了一半也得整包地带走。还有一层讲究咱们记下不展开:多个库和 .o 在命令行上的次序、循环依赖里的次序,都会影响抽取的成败,那是库搜索那篇的正题。

## 抽取之后:size 对照与自包含

咱们再从字节数这边对一遍:

```text
$ size lib_app direct_app app2
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	lib_app
   1351	    516	      4	   1871	    74f	direct_app
   1361	    516	      4	   1881	    759	app2
```

三条命令的变量,主要落在 extra.o 的身上:它进没进、进了给谁用,main 那头的差别,咱们后面单说。lib_app 的 text 是 1249,direct_app 的是 1351,多出来的 102 个字节,它的来路只有一处:整成员并进来的 extra.o。这个数您别去跟 size extra.o 单独报的 158 硬碰,合并的时候节的排布和对齐都会挪位置,逐字节地对不上,方向却是清楚的。app2 比 lib_app 多出的 112 个字节,它的来路有两条:102 是整成员并进来的 extra.o,剩下的 10,是 a2 的 main 比 a 的 main 大出来的,它多调了一次 bonus_a。判据也是现成的:size a.o 报的 text 是 123,a2.o 报的是 133,恰好差了 10。

代码进了可执行文件,库文件本身还用得着吗?咱们把 libmyutil.a 挪走再跑:

```text
$ mkdir -p stash && mv libmyutil.a stash/ && ./lib_app; echo $?
42
$ ./app2; echo $?
39
```

库里空了,咱们手里的两个程序照样跑。静态两个字落的地就在这儿:被抽进来的代码成了可执行文件本体的一部分,文件自己带的就是全份,运行的时候谁也不依赖,自包含说的就是这样的程序。开卷那篇里远远望过一眼的 collect2,张罗的就是这摊静态合并。这也埋下了它的短处:lib_app 和 app2 各自带了一份 helper,链它的程序到十万个,拷贝就有了十万份,b.c 修了一个 bug,链过它的程序就得统统重链。数不清的程序共用一份代码,静态库是给不了的,那是另一条路的事,咱们结尾说。

## CMake 视角

手工敲 ar rcs 再加 -l/-L 的这一套,日常的构建里都归 CMake 管,咱们把它的活偷看一眼,五句话就是最小的工程:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myutil_demo C)

add_library(myutil STATIC b.c extra.c)
add_executable(app a.c)
target_link_libraries(app PRIVATE myutil)
```

目标文件解剖那篇里咱们说过,add_library 不写类型的时候默认就是 STATIC。这儿咱们把类型显式写出来,读 CMakeLists 的人一眼就知道这个目标是静态库,不必再去翻别处的开关。您给它加一个 --verbose,命令就现了形:

```text
$ cmake --build build --verbose 2>&1 | grep -E "ar |ranlib"
/usr/sbin/ar qc libmyutil.a CMakeFiles/myutil.dir/b.c.o CMakeFiles/myutil.dir/extra.c.o
/usr/sbin/ranlib libmyutil.a
```

咱们看到的是两步。`ar qc` 干的是往档案里追加成员的活,q 的意思是只往尾部追加、不查同名替换,c 管的还是安静。写索引的活单独交给了 ranlib。您看,这正是 rcs 拆成两半的样子,s 的活 CMake 让 ranlib 干了,咱们从头到尾没碰一次 ar。链接的那头,同一次构建里的库,CMake 干脆把 libmyutil.a 的路径直接递给链接命令,-l 与 -L 的拼名约定它在背后替咱们扛了。

`target_link_libraries(app PRIVATE myutil)` 还是上一篇补全过的那句,表达的是链接期的依赖,PRIVATE 把依赖锁在了 app 自己的身上,下游的目标沾不着光。产物的落位也有开关:`CMAKE_ARCHIVE_OUTPUT_DIRECTORY` 您给它设一个目录,静态库的产物就统一落过去。CMake 的库还有一堆进阶的用法,咱们只认机制,展开的事归卷七。

## 各带一份的复用

咱们到这儿,上一篇结尾的想法办成了:b.o 和 extra.o 收进了一个 libmyutil.a,名字按约定起了,索引也写好了,链接器按 undefined reference 的点名抽取成员,抽进来的代码成了可执行文件的本体。咱们靠打包解决的,是 .o 的分发。可有一类特殊的复用,打包帮不上忙:模板、inline 的定义,是要跟着头文件满天用的,它们的合法性,下一篇专门给咱们讲。还有一类咱们想要的,是各带一份的静态库给不了的:数不清的程序共用同一份代码、库一升级全体受益,走的是动态库的路子,咱们每天链的 libstdc++ 和 glibc 就是那么用的。它的路是怎么铺的,咱们动态库总览那篇见。
