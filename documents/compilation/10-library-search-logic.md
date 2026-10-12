---
chapter: 13
difficulty: intermediate
order: 10
platform: host
reading_time_minutes: 16
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "库搜索逻辑:链接期与运行期怎么找库"
description: "链接期 -L 目录的次序拿两份同名库实测,再拿假 libz 遮蔽系统库看 -L 压默认目录;同目录 .a/.so 并存动态优先,-l: 与全路径改判;链接期三种找不到逐场景过,单遍扫描用 --trace-symbol 拍下轨迹;运行期换动态链接器经办,LD_PRELOAD、LD_LIBRARY_PATH、RUNPATH、ldconfig 缓存、受信目录逐档立证,RPATH 与 RUNPATH 一枚标签定先后;三件套三个名字各管一段,SONAME 缺省时 NEEDED 记什么也实测了;CMake 视角看 BUILD_RPATH 与 find_library。"
cpp_standard: [11, 14, 17, 20]
---

# 库搜索逻辑:链接期与运行期怎么找库

09 的结尾,咱们把工程侧天天撞的三个问题递到了今天:链接器到底按什么顺序找库?-lcalc 找的是 libcalc.so 还是 libcalc.a?运行期 LD_LIBRARY_PATH、系统目录这些又各排第几?这些问题还有更早的来路。01 路过库的时候也问过一串:链接了库还是找不到、静态动态谁排前面,明明在的库却说没有。当时咱们说好了,到时候一个场景一个场景地过。今天就是兑现的时候:链接期走了一条线,运行期走了另一条线,各排各的顺序,各认各的报错。

标本咱们接着用 calc 这个名字,内容却换成了一套会报身份的写法:库里的函数就叫 answer,谁被链了进来、谁被装了进来,返回值当场就报了号:

```c
// main.c —— 调用方:answer 返回几,选中的就是几号库
#include <stdio.h>
extern int answer(void);
int main(void) { printf("answer=%d\n", answer()); return 0; }
```

库笔者备了一排,answer 的取值从 1 排到 42,每份的差别只有返回值,拿它当认库的记号正合适。环境咱们也照例交代:笔者的实验都在 WSL2 里跑,工具链还是 GCC 16.2.1 配 binutils 2.47 的组合,glibc 用的是 2.44,CMake 用的也是 4.4.3,这一篇里的每一段输出,都是真实跑出来的。您那边的版本不同,报错的措辞、路径的长相这类细节可能有出入,机制本身倒是多年没变。

## 链接期:-L 目录的次序

链接器手里有现成的清单:您写下的每一个 -L,都往清单里添了一个目录,次序就照着书写的顺序排。光这么说您别信,咱们摆两份同名却不同内容的库:

```text
$ ar rcs dir_a/libcalc.a calc_a.o      # answer 返回 1
$ ar rcs dir_b/libcalc.a calc_b.o      # answer 返回 2
$ gcc main.o -Ldir_a -Ldir_b -lcalc -o app_a && ./app_a
answer=1
$ gcc main.o -Ldir_b -Ldir_a -lcalc -o app_b && ./app_b
answer=2
```

(行尾的注释是笔者加的,命令倒是真跑的。)两条命令就差在两个 -L 的书写顺序上:赢的那次,挑中的是 dir_a 里返回 1 的版本,咱们把两个 -L 一交换,返回 2 的就上了岗。链接器从左往右把目录挨个搜了一遍,头一个拼得出库名的目录拿到了库,排在后面的目录就不再看了。

除了咱们写的 -L,链接器自己也还带着一小串默认的目录,咱们拿 ld --verbose 问过它:自带的清单是 /usr/x86_64-pc-linux-gnu/lib64、/usr/lib、/usr/local/lib、/usr/x86_64-pc-linux-gnu/lib 这样一串,垫在所有 -L 的后面。这话咱们也不能光停在嘴上,拿系统里真有的库遮了一回:笔者编了一份假 libz.so,它的整个文件里只导出一个 answer,咱们把它放进 shadow 目录,而 main 按 -lz 要库:

```text
$ gcc -shared -fPIC -o shadow/libz.so calc_a.c
$ gcc main.o -Lshadow -lz -o app_shadow && ./app_shadow; echo $?
./app_shadow: symbol lookup error: ./app_shadow: undefined symbol: answer
127
$ ldd app_shadow | grep libz
	libz.so => /usr/lib/libz.so (0x00007779df4d8000)
```

这一场咱们得分两截说。咱们单看链接期:默认目录里躺着真的 libz.so,链接却过了,answer 在链接期解析到了 shadow 里的假货上,-L 的目录排在默认目录前面,这一层算是验到了。咱们再看运行期:动态链接器拿到 NEEDED 里的名字 libz.so,装进进程的却是 /usr/lib 的真 libz,它的里面没有 answer,main 的头一次调用当场翻车。名字倒是同一个,两位经办找库的人,各挑了各的文件。这场事故咱们记在这儿,等咱们讲到运行期再回来说它。

## 同名并存:.so 排在 .a 前面

目录定了,同一个目录里的文件也有讲究。06 咱们留过一个口子:同一目录里的 libfoo.a 和 libfoo.so 并排躺着,-lfoo 到底会拿谁?咱们真摆一对,静态库的 answer 返回 1,动态库的返回 7:

```text
$ cp dir_a/libcalc.a mixed/libcalc.a
$ gcc -shared -fPIC -o mixed/libcalc.so calc_so.c    # answer 返回 7
$ gcc main.o -Lmixed -lcalc -o app_mixed && LD_LIBRARY_PATH=mixed ./app_mixed
answer=7
$ readelf -d app_mixed | grep libcalc
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so]
$ gcc main.o -Lmixed -l:libcalc.a -o app_force_a && ./app_force_a
answer=1
$ nm app_force_a | grep answer
0000000000001161 T answer
$ gcc main.o ./mixed/libcalc.a -o app_path_a && ./app_path_a
answer=1
```

赢的是动态库:链接器进了目录去找 -lcalc 的候选,libcalc.so 已经在了,.a 就连被翻开的机会都没有,得等 .so 不在了,顶上的才会换成 .a。06 说好的两种后缀谁排前面的讲究,咱们现在交答案:动态的在前,静态的垫后。想越过这次的次序点名要 .a,您可以走 -l:libcalc.a,全路径的写法也行,咱们各跑了一遍,answer 都回到了 1,而且这回 answer 拿到的是带地址的 T,机器码并进了可执行文件,正是 06 讲过的静态链接老一套。08 里咱们也见过,直接写库文件的路径,链接器照样也收下了,还省得它在一屋子的 .a 和 .so 里挑,挑选的章法到这儿就交代齐了。

## 链接期的三种找不到

次序和后缀都立住了,该轮到兑现 01 的承诺:链接了库,它偏偏说找不到。咱们从头一个场景说起,库排错了位置。05 讲 undefined reference 排查的时候,咱们问过一句,是不是有个 .o 忘了挂上,还是定义住在一个没写的库里。库也写了,.o 也挂了,而第三种翻法还等着:库排在了命令行上引用它的 .o 前面。

```text
$ gcc -Ldir_a -lcalc main.o -o app_order
/usr/bin/ld: main.o: in function `main':
main.c:(.text+0x5): undefined reference to `answer'
collect2: error: ld returned 1 exit status
$ gcc main.o -Ldir_a -lcalc -o /dev/null -Wl,--trace-symbol=answer
/usr/bin/ld: main.o: reference to answer
/usr/bin/ld: dir_a/libcalc.a(calc_a.o): definition of answer
```

咱们只把 main.o 挪到了 -lcalc 的左边,链接就过了,库的一个字节都没动,靠的正是 06 交代过的次序:库要放在引用的后面。背后的原因是链接器的单遍扫描:它从左到右把命令行走了一遍,走过的文件不再回头。轮到 libcalc.a 的时候,它看一眼手头悬着的 U 名单,main.o 的引用还没到,名单是空的,索引也就白查了,所以一个成员都没抽,等 main.o 随后把 answer 挂上了 U,前面那个 .a 文件早就走过去了。库与库之间的次序,咱们也得照着守:库自己也会引用别的库,A 库用了 B 库的符号,-lB 就得排在 -lA 的右边,道理跟 .o 写在库的前面是同一条。两个库要是互相用上了对方的符号,总有一边的符号会落空,06 记下不展开的循环依赖说的就是它,解法咱们有两条。头一条是把 -l 的开关再写一遍,末尾那一遍补齐前一轮落下的符号,排下来就成了 -lA -lB -lA。另一条用的开关是 --start-group 和 --end-group,两个库写在开关的中间,链接器会在组里反复地扫,直到再也抽不出新的成员。

空口说总是虚的,链接器还能自己招供:咱们给链接命令添上 --trace-symbol=answer 的开关,它就报出了这个符号到过的每一站。上面链接成功的跑法,trace 里 reference 和 definition 一前一后地到齐,成员被抽走了。咱们把库挪到前面再跑一遍,trace 里只剩下了 reference 一行,definition 再也没出现了,错误也照旧了。

有个实情笔者也得报给您:同样的错位,咱们把 .a 换成 .so,在这套 GCC 16.2.1 上居然链接成功了,answer 照样跑出了 7。而动态库没有抽取一说,链接器把它整个登了记,后面的 .o 引用它的时候,登记是现成的,所以这个场景的受害者主要是 .a。您那边要是同样的命令报了 undefined reference,多半是发行版给 gcc 默认补了 --as-needed,它只登记当时已经有人要的库,排在引用前面的 .so 会被整个丢下。咱们这套链出来的命令里没有这个开关,笔者拿 gcc -### 翻过真实的链接行,两个方向的输出里都没出现。

咱们看第二个场景,名字不合了约定。笔者把库复制进 naming 目录,顺手摘了 lib 的前缀:

```text
$ cp dir_a/libcalc.a naming/calc.a
$ gcc main.o -Lnaming -lcalc -o app_naming
/usr/bin/ld: cannot find -lcalc: No such file or directory
/usr/bin/ld: note to link with naming/calc.a use -l:calc.a or rename it to libcalc.a
collect2: error: ld returned 1 exit status
```

同样的报错您在 06 见过,今天补上当时欠下的角度:为什么这个目录搜了也是白搜。咱们要它找的候选名只有 libcalc.so 和 libcalc.a,而 naming 里的文件叫 calc.a,所以跟哪个候选都拼不上。其实链接器并非看不见这个文件,只是候选的名单上没有这一号。note 递的两条路也各有去处:-l:calc.a 绕开了拼名,名字改回 libcalc.a 就重新入了列。

第三个场景最冤:库就躺在咱们的当前目录里,咱们却一个 -L 都没写:

```text
$ gcc main.o -lcalc -o app_bare
/usr/bin/ld: cannot find -lcalc: No such file or directory
collect2: error: ld returned 1 exit status
```

mixed 目录里的 libcalc.a 和 libcalc.so 都在,可链接器的目录清单只认 -L 添的和自己带的那串默认目录,咱们当前所在的目录是挤不进去的,您想让它进来,写一个 -L. 就够了。您日后对着 cannot find 排查,咱们要查的方向也就三个:名字合不合约定?目录在不在清单?库有没有排在引用的后面?

## 运行期:换了一位找库的人

还记得 shadow 那一场吗?链接期挑中的,是 -L 目录里的假 libz,运行期装进来的,却是 /usr/lib 的真 libz。08 留下的问题到这儿就能答全了:为什么链接期顺顺当当的,运行期还要 LD_LIBRARY_PATH 伺候?因为两个阶段找库的根本不是同一位经办人。链接期出面的是链接器,它认 -L 和自带的默认目录,等链接做完了,-L 的信息没有落进可执行文件的任何地方。运行期出面的是动态链接器,01 的 file 输出末尾 interpreter 的真身,08 里逐行读 NEEDED 的经办人,说的都是它。它从头到尾没见过咱们的链接命令,自然也就不认识 -L 了。09 里咱们一直写的 LD_LIBRARY_PATH=.,现在您说得清它干的事了:把当前目录补进环境变量的一档。

它认的档位,咱们逐档实测。SONAME 咱们在 08 认过,是库给自己登记的正式名,这回备下的三份库内容不同,登记的 SONAME 都是 libcalc.so.1,分放进了三个目录里,answer 的取值分别是 10、20、30。程序预埋了一条运行期的搜索路径,靠的是 -rpath 这个链接器开关,链接器把路径写进了动态段,就成了 RUNPATH。链接时咱们直接写 rpath_dir 里的路径,免得又牵出链接期选谁的问题,NEEDED 登记的只是 SONAME,链接用的是哪份文件,运行期已经不记得了:

```text
$ gcc -shared -fPIC -Wl,-soname,libcalc.so.1 -o rpath_dir/libcalc.so.1 calc10.c
$ gcc main.o ./rpath_dir/libcalc.so.1 -Wl,-rpath,/tmp/…/rpath_dir -o app_rpath
$ readelf -d app_rpath | grep -E "libcalc|PATH"
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
 0x000000000000001d (RUNPATH)            Library runpath: [/tmp/…/rpath_dir]
$ ./app_rpath
answer=10
$ LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_rpath
answer=20
$ LD_PRELOAD=/tmp/…/preload/libcalc.so.1 ./app_rpath
answer=30
```

(长路径笔者裁成 /tmp/… 了,其余是原样的。)三行结果咱们分头认:裸跑的时候,装进来的是 RUNPATH 指的 rpath_dir 里返回 10 的版本,咱们再设上 LD_LIBRARY_PATH,返回 20 的反把 RUNPATH 压了下去,咱们最后上 LD_PRELOAD,连搜索都省了,预载的库被头一个映射进进程,它登记的 SONAME 也是 libcalc.so.1,NEEDED 那行当场有了主,后面的整场搜索都不再发生了。咱们拿 LD_DEBUG=libs 数过,轨迹里的 find library=libcalc 一行都没有,数出来的结果是 0。

完整的次序咱们不用背,LD_DEBUG=libs 的搜索轨迹把每一档的名字都标在了行尾。程序的两个依赖各搜了一遍,咱们拿 grep 的模式筛了两遍,剩下的行咱们从头读:

```text
$ LD_DEBUG=libs LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_rpath 2>&1 | grep -E "find library|search path=.*(LD_LIBRARY_PATH|RUNPATH)|search cache|trying file=/usr/lib/libc"
   1648884:	find library=libcalc.so.1 [0]; searching
   1648884:	 search path=/tmp/…/lp_dir/glibc-hwcaps/x86-64-v3:/tmp/…/lp_dir/glibc-hwcaps/x86-64-v2:/tmp/…/lp_dir		(LD_LIBRARY_PATH)
   1648884:	find library=libc.so.6 [0]; searching
   1648884:	 search path=/tmp/…/lp_dir		(LD_LIBRARY_PATH)
   1648884:	 search path=/tmp/…/rpath_dir/glibc-hwcaps/x86-64-v3:/tmp/…/rpath_dir/glibc-hwcaps/x86-64-v2:/tmp/…/rpath_dir		(RUNPATH from file ./app_rpath)
   1648884:	 search cache=/etc/ld.so.cache
   1648884:	  trying file=/usr/lib/libc.so.6
```

(行首的那串数字是进程号,您跑出来的不会一样。)咱们顺着往下读:libcalc 在 LD_LIBRARY_PATH 的一档就命中了,所以它的搜索到第二行就停,libc 在同一档扑了空,接着轮到了 RUNPATH,再往后的档是 search cache=/etc/ld.so.cache,缓存的名单里正好有 libc,一步就落到了 /usr/lib/libc.so.6。您再看路径里夹着的 glibc-hwcaps/x86-64-v3 之类的子目录,那是 glibc 2.33 起按 CPU 指令集档位挑优化版本的路数,咱们本篇用不上,您认目录本身就行。缓存要是也没有呢?最后一档是系统的默认目录,它的原形咱们马上看。

LD_LIBRARY_PATH 压得过 RUNPATH 的这个次序,老资料里常写成反的。资料的作者往往没有错,真正换过代的是标签:动态段里写运行期搜索路径的标签有两枚,旧式的名字叫 DT_RPATH,新式的名字叫 DT_RUNPATH,链接器如今默认产出的是新式,这一档的默认由 --enable-new-dtags 开关把着,加上 --disable-new-dtags 就退回了旧式。落到搜索顺序上的分别就一条:RPATH 排在 LD_LIBRARY_PATH 的前面,RUNPATH 排在它的后面。咱们用旧式标签把同一个程序重链一遍:

```text
$ gcc main.o ./rpath_dir/libcalc.so.1 -Wl,--disable-new-dtags,-rpath,/tmp/…/rpath_dir -o app_oldrpath
$ readelf -d app_oldrpath | grep -E "PATH"
 0x000000000000000f (RPATH)              Library rpath: [/tmp/…/rpath_dir]
$ LD_LIBRARY_PATH=/tmp/…/lp_dir ./app_oldrpath
answer=10
```

同样的环境变量、同样的两份库,这回赢的是 10,RPATH 把 LD_LIBRARY_PATH 压了回去,与 RUNPATH 那一场正好反了过来。您哪天在两台机器上见到了相反的实验结果,多半就是这一枚标签的差别,readelf -d 一看就知道了。

## 明明在,却说找不到

08 里留过一个更憋屈的情形:文件明明是在的,它偏偏说找不到。咱们把这一幕摆出来,咱们这回没给程序埋 RUNPATH,库就躺在旁边的 rpath_dir 目录里,链接用的就是它:

```text
$ gcc main.o ./rpath_dir/libcalc.so.1 -o app_norpath
$ ./app_norpath; echo $?
./app_norpath: error while loading shared libraries: libcalc.so.1: cannot open shared object file: No such file or directory
127
$ LD_DEBUG=libs ./app_norpath 2>&1 | grep -E "find library|search cache|search path=.*system|trying file=/usr/lib/libcalc|no such file|error while"
   1648898:	find library=libcalc.so.1 [0]; searching
   1648898:	 search cache=/etc/ld.so.cache
   1648898:	 search path=/usr/lib/glibc-hwcaps/x86-64-v3:/usr/lib/glibc-hwcaps/x86-64-v2:/usr/lib		(system search path)
   1648898:	    (no such file)
   1648898:	    (no such file)
   1648898:	  trying file=/usr/lib/libcalc.so.1
   1648898:	    (no such file)
./app_norpath: error while loading shared libraries: libcalc.so.1: cannot open shared object file: No such file or directory
```

您在轨迹里挨个数:咱们没埋 RUNPATH,咱们也没设 LD_LIBRARY_PATH,缓存里也没有它的影子,最后搜到了 system search path 一档,也就是系统的默认目录,文档里给它的称呼是受信目录(trusted directories),咱们在 /usr/lib 一路看到的都是 (no such file)。当前目录呢?哪个档位里都没有它的位置,处境和链接期的场景三一模一样:所以那边不添 -L 就进不了清单,这边连 -L 都没有了,更没有谁捎它了。咱们想让这样的库被找到,可走的路子有三条:头一条是把文件挪进受信目录,root 权限也少不了,挪完了还得请 ldconfig 出面,它是 glibc 家管缓存的工具,扫受信目录和 /etc/ld.so.conf.d 里登记的目录,把名字到路径的对照写进 /etc/ld.so.cache。第二条的路是用 LD_LIBRARY_PATH 或 RUNPATH,把目录补进搜索的某一档。第三条是在发布的时候把路径写进 RUNPATH。笔者的 WSL 没有免密提权,这一步笔者没有真做,咱们只读地看一眼缓存长什么样:

```text
$ ldconfig -p | grep "libz\.so$"
	libz.so (libc6,x86-64) => /usr/lib/libz.so
```

缓存档真的被用起来,上一节 libc 的轨迹已经演过了:search cache 的一行之后,直接落到了 trying file=/usr/lib/libc.so.6,一步就命中了。找不到的那一场,就是这一行的后面跟着 (no such file),兜底的受信目录也空了,报错这才落了地。链接期的 cannot find 和运行期的 cannot open shared object file,咱们这一篇都见过了:报错的抬头一个是 /usr/bin/ld,一个是程序自己的名字,哪一位在说话?抬头一对照就分清了。

## 三件套:三个名字各管一段

顺序讲完了,08 留给咱们的三件套怎么摆,今天咱们也一并补上。真身的文件名带完整的版本号,咱们编的是 libcalc.so.1.2.3,行话里的叫法是 realname、真实名。第二个名字是正式名的 libcalc.so.1,咱们用 soname 开关登记,软链也配上了一条。第三个名字是给开发链接用的,它的行话叫链接名(linker name),就是 libcalc.so 的软链,06 的 -l 拼名拼出来的正是它:

```text
$ gcc -shared -fPIC -Wl,-soname,libcalc.so.1 -o trio/libcalc.so.1.2.3 calc42.c
$ ln -s libcalc.so.1.2.3 trio/libcalc.so.1 && ln -s libcalc.so.1 trio/libcalc.so
$ readelf -d trio/libcalc.so.1.2.3 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ gcc main.o -Ltrio -lcalc -o app_trio
$ readelf -d app_trio | grep libcalc
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
$ LD_LIBRARY_PATH=trio ./app_trio
answer=42
```

咱们把一次链接从头走到尾,三个名字各管的哪一段,咱们挨个认:-lcalc 拼出的是链接名 libcalc.so,链接器顺着软链摸到了真身,读出它登记的 SONAME,写进 NEEDED 的是 libcalc.so.1,运行期的动态链接器拿这个名字去搜,命中的是软链 libcalc.so.1,软链再指回了真身,42 就是这么进来的。升级的时候换真身、挪软链,可执行文件的一个字节都不用动,08 说过的那个好处,落到文件上的就是这个摆法。版本号的三段各记什么、什么样的变更该动哪一段,这些事咱们交给 12(ABI 设计)。

NEEDED 记的既然是 SONAME,SONAME 改了,NEEDED 跟不跟着走?咱们把 soname 登记成 libcalc.so.9 重链一份,真身的文件名照旧:

```text
$ gcc -shared -fPIC -Wl,-soname,libcalc.so.9 -o soname/libcalc.so.1.0.0 calc42.c
$ gcc main.o ./soname/libcalc.so.1.0.0 -o app_soname9
$ readelf -d app_soname9 | grep calc
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.9]
$ ./app_soname9
./app_soname9: error while loading shared libraries: libcalc.so.9: cannot open shared object file: No such file or directory
$ ln -sf libcalc.so.1.0.0 soname/libcalc.so.9
$ LD_LIBRARY_PATH=soname ./app_soname9
answer=42
```

NEEDED 真的跟着走了,而且认的只是 SONAME,真身的文件名叫什么,NEEDED 是不管的,目录里没有叫 libcalc.so.9 的文件,运行期的搜索照样扑空,补一条同名软链就活了。那 SONAME 缺省的时候呢?行为就分了两种,咱们都实测了。按 -l 搜到的库,NEEDED 记的是文件名,08 手工编的 libmyutil.so 登记的正是 libmyutil.so,它的来路就在这儿。写全路径链的库,NEEDED 记的居然是整条带斜杠的路径:

```text
$ gcc -shared -fPIC -o soname/libcalc_noson.so.5 calc42.c
$ gcc main.o ./soname/libcalc_noson.so.5 -o app_noson
$ readelf -d app_noson | grep calc
 0x0000000000000001 (NEEDED)             Shared library: [./soname/libcalc_noson.so.5]
```

带斜杠的依赖名还牵出一个运行期行为:动态链接器见到名字里的斜杠,就不搜索了,把它当相对启动目录的字面路径直接打开。咱们换到别的目录跑过,LD_LIBRARY_PATH 设得再长也救不回它了。所以正经发布的库都要登记 SONAME,把名字定成不带斜杠的裸名,搜索的机制才接得上手。

## Windows 的同款问题

咱们这一篇讲的都是 ELF 这一套,Windows 那边的大结构是相同的,各处的细节倒是不同,06 说好的对照,今天咱们只对照一小段。链接期的搜索,MSVC 家找的是 .lib:静态库的文件是它,给 DLL 配的导入库也是它,一个后缀担了两种身份,搜索的目录由 LIB 环境变量供给,拼名也没有 lib 前缀的讲究,foo 的库就叫 foo.lib。运行期的装载器找的是 DLL,头一个搜的就是 EXE 自己的目录,往后排的是系统目录,PATH 的位置垫在最末。有一处两边正好反了过来,您对照着看:咱们这边,库躺在程序旁边是不被理会的,上一节的那场 cannot open 刚演过,Windows 那头却是反着来的,DLL 和 EXE 放一起反而是一等正路,装载器天然地从最前头搜起。链接、运行两阶段是分头搜的,这个大结构是两边共通的,再往细处的对照,咱们交给 12(ABI 设计)。

## CMake 视角

手工调 -L、手写 -rpath 的这些活,日常的构建里都归 CMake 管。运行期的那串路径,咱们在 CMake 里有专门的地方写:

```cmake
add_library(calc SHARED calc_c.c)
add_executable(app main_c.c)
target_link_libraries(app PRIVATE calc)
set_target_properties(app PROPERTIES BUILD_RPATH "/opt/calc_runtime")
```

BUILD_RPATH 管的是构建期产物的运行期搜索路径,咱们随手写了个还不存在的 /opt/calc_runtime。咱们带上 --verbose 看链接行,它真落进了链接行的 rpath 开关:

```text
$ cmake --build build --verbose 2>&1 | grep "cc .*-o app"
/usr/sbin/cc … CMakeFiles/app.dir/main_c.c.o -o app  -Wl,-rpath,/opt/calc_runtime:/tmp/…/build libcalc.so
```

(链接行的中段和长路径,笔者照例裁成了 … 和 /tmp/…,其余是原样的。)您看链接行里的路径有两条:咱们写的 /opt/calc_runtime 在头里,CMake 自己补的构建目录在后头。08 里那行只说了是 CMake 顺手写进、没说为什么的 RUNPATH,今天补上为什么:CMake 见到链接的目标在构建树里,顺手把构建目录也写了进去,构建树里的 app 裸跑就能找到同树的库。咱们还有全局的 CMAKE_BUILD_RPATH 可以统一添,用法也是同款的。

-L 的那一头呢?06 的 CMake 节咱们见过,同一次构建里的库,CMake 直接把 libmyutil.a 的路径递给链接命令,-L 和拼名都替咱们扛了。库要是在工程的外面,出面找它的是 find_library,找到的会是一条绝对路径:

```cmake
find_library(CALC_LIB NAMES calc PATHS ${CMAKE_CURRENT_SOURCE_DIR}/libs NO_DEFAULT_PATH)
target_link_libraries(app PRIVATE ${CALC_LIB})
```

```text
-- find_library 拿到: /tmp/…/libs/libcalc.so
$ cmake --build build --verbose 2>&1 | grep "cc .*-o app"
/usr/sbin/cc … main_c.c.o -o app  -Wl,-rpath,/tmp/…/libs /tmp/…/libs/libcalc.so
```

链接行里 -L 和 -l 都没了,直接给的是绝对路径,连带着库目录也被写进了 RUNPATH,构建出来的 app 裸跑就过,咱们实测拿到的正是 42。至于咱们没用的 target_link_directories 和更老的 link_directories,取舍咱们一句话说完:前者是按目标生效的,后者是目录级的一刀切,子目录全都沾上了,而 CMake 自己的推荐里,这俩都排在 find_library 的后头,路径已经找成了确定的,比咱们塞一串 -L 让链接器挨个去碰要稳当。

## 接下来:同名符号,谁说了算

两条线到这儿都走顺了:链接期的线里,链接器拿 -L 的目录清单和拼名约定把库找到,该登记的登记,该抽取的抽取,运行期的线里,动态链接器按预载、环境变量、RUNPATH、缓存、受信目录的次序,把库找齐、也映射了进来。可库都进了进程,事情也只办了一半:main 调 answer 的时候,进程里的好几份导出表都开着,排头的是谁?顶替者凭什么赢?咱们怎么护住自己的符号?又怎么主动把口子收窄?09 的预载实验露过一角,11(符号可见性)整篇管的就是这一件事。
