---
chapter: 13
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 工具链
- CMake
title: "动态库总览:构建期与运行期的两阶段"
description: "gcc -shared -fPIC 做出 libmyutil.so;链接器对动态库不拷代码,把依赖登记进动态段的 DT_NEEDED,拿 nm 与 size 对拍验证;运行期动态链接器按登记找库、映射进内存,ldd 看清单,挪走 .so 看真实的启动报错;再起两个进程,拿 maps 与 smaps 对拍,看代码页怎么在物理内存里只住一份;最后用 CMake 的 SHARED 与 VERSION/SOVERSION 看三件套命名。"
cpp_standard: [11, 14, 17, 20]
---

# 动态库总览:构建期与运行期的两阶段

上一篇的结尾,咱们把一个问题递到了今天:libstdc++、glibc,凭什么能被数不清的程序共用?这个问题的来路还有前史。静态库那篇里咱们记下过短处:链过它的每个可执行文件都自带一整份拷贝,链它的程序到了十万个,拷贝也就有了十万份,b.c 修了一处 bug,十万次重链一次也免不了。开卷那篇里还埋过半句话:collect2 默认捎上的那排库,以动态依赖的方式挂上,代码不进咱们的文件,到底怎么挂的,当时卖了个关子。今天咱们把这三句话一起兑现。

思路也替咱们预告过了:构建期登记、运行期接上,展开的说法是:到了构建期,面对动态库的链接器只做登记,把对它的需要写进了可执行文件,库的代码一个字节不进来。到了运行期,程序启动的时候,动态链接器按登记把每一项库找到、映射进内存、把地址也接好了,然后才轮到咱们写的 main。库自己单独成了一个文件,躺在硬盘的目录里。咱们今天做的这个就是动态库,英文的叫法是 shared library,中文叫的是共享库,Linux 上的后缀是 .so:分发的单位变了,库独立成了文件,大家共用的是同一个文件,程序身上留下的只是几行登记。

环境咱们照例交代:笔者的实验都在 WSL2 里跑,工具链还是 GCC 16.2.1 配 binutils 2.47 的组合,glibc 用的是 2.44,CMake 用的也是 4.4.3,这一篇里的每一段输出,都是真实跑出来的。您那边的版本不同,字节的多少、地址的长相这类细节可能有出入,机制本身倒是多年没变。

两阶段的经办人,一位是链接器那篇里咱们盯过全程的链接器,另一位是动态链接器——开卷第一篇里 file 输出末尾的 interpreter 的真身,身份当时就揭晓了,今天咱们直接叫本名。各自的活和查看的位置,咱们一张表摆开,原料还是链接器那篇的原班:a.c 挂着 shared_seed 和 helper 两条 extern 声明,b.c 管的是定义,10 加 32 的结果是 42。

| 阶段 | 经办人 | 干的事 | 在哪儿看 |
|------|--------|--------|----------|
| 构建期 | 链接器 | 找到 .so、核对符号、登记依赖,不拷代码 | readelf -d 的 NEEDED 行 |
| 运行期 | 动态链接器 | 按登记找库、映射进内存、接地址 | /proc 里进程的 maps 文件 |

## 构建 .so:-shared 与 -fPIC

做动态库的命令只有一条,咱们顺手把产物也验了:

```text
$ gcc -shared -fPIC -o libmyutil.so b.c
$ readelf -h libmyutil.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x0
```

两个选项咱们各认一遍。-shared 是给链接器的指令:产物按动态库的规格出,ELF 的类型标成 DYN,启动文件的那一排不挂,连 main 都不找了——b.c 里压根没有 main,链接也照样过了,链接器那篇里 no_main 的那场报错在这儿不会上演。-fPIC 的全称是 Position Independent Code、位置无关代码,它让编出来的机器码被映射到任何地址,都能正确地执行。为什么非它不可?同一个库会被不同的进程,映射到各不相同的地址,这一点您到后面运行期的一节亲眼见,机器码层面它是怎么做到的,下一篇整篇讲的就是这个。DYN 这个类型您在目标文件解剖那篇见过另一半:hello_app 那样的 pie 可执行文件,Type 栏写的也是 DYN。把两位分开的是 Entry 一栏:pie 可执行文件是带入口的,那篇里说它指向的是 `_start`。眼前这个 .so 的 Entry 是 0x0,等于明说了没有入口,它自己是不跑的,被别人映射才是它的活。您要是好奇它能不能被当成可执行文件跑起来,番外那篇专门玩的就是这个。

## 登记在动态段:DT_NEEDED

库有了,该轮到调用方了。链接命令您在静态库那篇刚练过:

```text
$ gcc -c a.c -o a.o
$ gcc a.o -L. -lmyutil -o dyn_app
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
42
```

拼名的约定还是一个字没变,干的还是 -L 添目录、-l 按名字要库的老活。变的在链接器那头:同一句 -lmyutil,静态库那篇里它解开的是 libmyutil.a,抽了成员并进程序,这回目录里躺的是 .so,它干的活就换成登记了。多出来的 LD_LIBRARY_PATH,是往运行期找库的路径里添目录用的。为什么链接期一帆风顺,运行期还要它伺候?搜索的章法归库搜索那篇,本篇它只负责让实验目录里的 ./dyn_app 跑起来。42 还是那个熟悉的 42。登记写在了哪儿,readelf -d 带咱们看,它读的是动态段(dynamic section),ELF 里专供动态链接使用的一块,咱们的依赖清单就躺在里面:

```text
$ readelf -d dyn_app | head -5
 (输出顶上的空行笔者裁了)
Dynamic section at offset 0x2dd0 contains 27 entries:
 (表头一行笔者裁了)
 0x0000000000000001 (NEEDED)             Shared library: [libmyutil.so]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
```

咱们拿到两行 NEEDED,它们的学名是 DT_NEEDED,DT 拿的是 dynamic tag、动态标签的打头。头一行的 libmyutil.so,是咱们亲手登记的。第二行的 libc.so.6,就是 collect2 每回都默认捎的那排库里的一员,那排库咱们在开卷那篇见过。现在您看到它们挂上的样子了,挂的意思,就是往动态段里写了一行名字。开卷第一篇卖的关子到这儿兑现了一半。库的身上也有动态段,libmyutil.so 里同样有一行 NEEDED 记着 libc.so.6:库依赖库的路也是通的,清单各记各的。

## 代码进没进来:nm 与 size 对拍

登记与拷贝的分别,咱们拿三样东西来验,头一样看的是符号:

```text
$ nm dyn_app | grep -E "helper|shared_seed"
                 U helper
0000000000004018 B shared_seed
```

helper 拿的还是 U。静态链接的那一头,静态库那篇里 lib_app 的 helper 已是带地址的 T,b.o 的机器码并了进去。眼下的 dyn_app 里,它是条悬而未决的引用,定义原封不动地留在了 libmyutil.so 里。shared_seed 的那行看着蹊跷:它怎么在可执行文件里有了住处,还落在了 bss?这是链接器给数据符号另办的一种手续,把变量的存储安排到了可执行文件名下,运行的时候库那边再对过来,机器码层面的讲究归下一篇,您且把现象记下。

第二样看的是体积。咱们拿同一条 a.o,分别往静态、动态的两条路上链:

```text
$ gcc -c b.c -o b.o && ar rcs libmyutil.a b.o && gcc a.o ./libmyutil.a -o static_app
$ size static_app dyn_app
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	static_app
   1472	    600	      8	   2080	    820	dyn_app
```

直接写 libmyutil.a 的路径,链接器也是照收的,静态库那篇里咱们试过,还省得它在一屋子的 .a 和 .so 里挑,挑选的章法归库搜索那篇。结果您八成没料到:动态链出来的可执行文件,反倒大了。大多少得说清口径:size 的 text 一栏从 1249 涨到了 1472,大了 223 字节。您要是拿 ls -l 称磁盘上的文件,量出来的是 15872 对 15992,差的是 120 字节,两个数各是各的口径。库的代码确实没进来,可动态链接自己的东西进来了。咱们拿 size -A 分节看,涨的集中在伺候动态链接的那批节:.dynamic 从 416 涨到了 496,.dynsym 与 .dynstr——动态符号表和它的字符串表——各厚了几十字节,还新添了 32 字节的 .plt(PLT 这个名字,开卷那篇里远远地望过一眼,函数调用走的跳板,正题是下一篇的事)和 24 字节的 .rela.plt。真正的代码 .text 反而从 279 降到了 268,差出来的 11 个字节,正是 helper 的机器码,咱们拿 readelf -S 量 b.o,它的 .text 拢共就这 11 个字节。对这么小的库来说,登记的开销反倒比库代码本身还贵。库要是不止这么大呢?咱们把库喂胖 4KB 再拍:

```c
int shared_seed = 10;
int helper(void) { return 32; }
const int big_table[1024] = {1, 2, 3};   // b_fat.c:同一成员里的 4KB 常量表
```

```text
$ gcc -shared -fPIC -o libmyfat.so b_fat.c && gcc -c b_fat.c -o b_fat.o && ar rcs libmyfat.a b_fat.o
$ gcc a.o ./libmyfat.a -o static_fat_app && gcc a.o -L. -lmyfat -o dyn_fat_app && size static_app static_fat_app dyn_app dyn_fat_app
   text	   data	    bss	    dec	    hex	filename
   1249	    516	      4	   1769	    6e9	static_app
   5373	    516	      4	   5893	   1705	static_fat_app
   1472	    600	      8	   2080	    820	dyn_app
   1471	    600	      8	   2079	    81f	dyn_fat_app
```

静态的那一边,4KB 的表把 b_fat.o 的整个成员拽了进来(静态库那篇里咱们讲过,抽取的最小单位是成员),text 涨了 4124。动态的那一边,1472 变成了 1471,差的那个 1,是登记的库名短了一个字符:libmyfat.so 比 libmyutil.so 少一位,.dynstr 也就跟着小了 1 字节。咱们拿 readelf -S 把两边逐节对过,除了 .dynstr 分毫未动,其实等于没动。咱们这组对拍把只登记、不拷贝的分别落定了:可执行文件的肥瘦,和静态库的内容是同涨同消的,跟动态库的内容毫无关系,它身上带走的只有几行登记。

## 运行期:动态链接器按登记接库

程序跑起来的时候,头一件办的事不是 main。开卷第一篇里 file 输出末尾的 interpreter,它的真身是动态链接器,一个住在 /lib64/ld-linux-x86-64.so.2 的程序。内核装载可执行文件的时候瞧见了它,就赶在前面把动态链接器也装了进来、从它开跑:它读的是 NEEDED 清单,把每一项找到、打开、映射进自己的地址空间,把该接的地址接上,再把控制权交给程序的 `_start`。这整套流程的细枝末节归程序启动那篇,咱们今天只看它的两头,清单本身咱们用 ldd 就能看:

```text
$ LD_LIBRARY_PATH=. ldd dyn_app
	libmyutil.so => ./libmyutil.so (0x000076c91d16f000)
	libc.so.6 => /usr/lib/libc.so.6 (0x000076c91ce00000)
	/lib64/ld-linux-x86-64.so.2 => /usr/lib64/ld-linux-x86-64.so.2 (0x000076c91d195000)
```

咱们怎么读:左列是名字,=> 右边是实际找到的路径,括号里是它被安排的地址。咱们登记的 libmyutil.so 按 LD_LIBRARY_PATH 找到了 ./libmyutil.so,libc 找到了系统的 /usr/lib/libc.so.6。被裁掉的 vdso 那行是内核映射进来的一小段代码,让读时钟的高频操作不用陷进内核,与咱们的主题无关。最有意思的是末一行:动态链接器把自个儿也列上了,它自己的身份也是 .so。

清单上有一项找不到会怎样?咱们把库挪走,真跑一回给您看:

```text
$ mkdir -p stash && mv libmyutil.so stash/ && LD_LIBRARY_PATH=. ./dyn_app; echo $?
./dyn_app: error while loading shared libraries: libmyutil.so: cannot open shared object file: No such file or directory
127
```

眼前的报错您多半眼熟。它和链接器那篇的 undefined reference,是两个阶段的错:那一边是构建期的链接器报的,拦的是链接。这一边是运行期的动态链接器报的,报错一出来进程就直接退出了,退码给的是 127,main 的一行都没轮到跑。咱们把库 mv 回来,程序立刻又能跑了,可执行文件的一个字节都没动。还有一种更憋屈的情形:文件明明是在的,它偏偏说找不到,那是搜索路径没照顾到的缘故,排查的路数归库搜索那篇。

## 共用怎么成立:两个进程,同一页

现在回到链接器那篇立下的主问题:数不清的程序,凭什么能共用?咱们的观察窗口是 /proc:每个进程在 /proc 底下都有自己的目录,PID 就是目录的名字,里面的 maps 文件把这个进程地址空间里的映射一行一行地列着。笔者备了个三行的调用方 d_hold.c,好把进程留住给咱们看:它声明了 helper,调一回就把返回值存进了局部变量,然后用 pause() 挂起了自己。咱们把它链上动态库、起两个实例,记下的两个进程号是 P1、P2。这对文件笔者挪到了磁盘的目录,又额外 sync 了一回,缘由您看到下面 smaps 的 Clean 一栏就明白了(smaps 是 maps 旁边的姊妹文件,它给每段映射多记了一组计数):

```text
$ cp libmyutil.so hold_app /var/tmp/dynlib_share/ && sync
$ LD_LIBRARY_PATH=/var/tmp/dynlib_share /var/tmp/dynlib_share/hold_app & P1=$!
$ LD_LIBRARY_PATH=/var/tmp/dynlib_share /var/tmp/dynlib_share/hold_app & P2=$!
$ grep libmyutil.so /proc/$P1/maps
7343e14bd000-7343e14be000 r--p 00000000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
7343e14be000-7343e14bf000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
(中间两段只读的,笔者裁了)
7343e14c1000-7343e14c2000 rw-p 00003000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
$ grep libmyutil.so /proc/$P2/maps
7eef9ec3c000-7eef9ec3d000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
(P2 其余各行与 P1 行行同构,笔者裁了)
```

咱们要看的要点有两条。头一条说的是地址:两个进程把同一个库映射到了各自的地址上,虚拟地址是各用各的,每个进程的地址空间本来就各管各的,真正共用的,是物理内存的那一层。另一条说的是 inode:行里的 08:30 757322 那两列,记的是设备号和 inode 号,inode 是文件在文件系统里的编号,两个进程对出来的是同一个 757322,映射的原来是同一个文件。物理的那一层 maps 看不见,得请它旁边的 smaps 出面:同样的映射,每段的底下多记了一组计数,Shared 打头的几行,计的是这段里与别的进程共着的物理内存。咱们把两个进程的代码段(r-xp)各抽出来:

```text
$ awk '/r-xp.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P1/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Shared_Dirty|Private_Dirty):"
7343e14be000-7343e14bf000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          4 kB
Shared_Dirty:          0 kB
Private_Dirty:         0 kB
$ awk '/r-xp.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P2/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Shared_Dirty|Private_Dirty):"
7eef9ec3c000-7eef9ec3d000 r-xp 00001000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          4 kB
 (两行零值与 P1 的相同,笔者裁了)
```

两个进程的这一页代码,都记着 Shared_Clean 的 4 kB:同一页的物理内存,数记在两个进程的头上。Clean 说的是页的内容与磁盘上的文件一致,咱们提前 sync 就是冲它去的。您若赶在文件刚写出的时候看,或者您的 /tmp 是内存盘,那一栏记下的会是 Dirty,不要紧的,认准 Shared 的字样就行。对照组用的是数据段(rw-p),咱们看 P1 的(P2 的三个数与之分毫不差):

```text
$ awk '/rw-p.*libmyutil\.so/{p=1;print;next} p&&/^[0-9a-f]+-/{exit} p{print}' /proc/$P1/smaps | grep -E "^[0-9a-f]+-|^(Rss|Shared_Clean|Private_Dirty):"
7343e14c1000-7343e14c2000 rw-p 00003000 08:30 757322                     /var/tmp/dynlib_share/libmyutil.so
Rss:                   4 kB
Shared_Clean:          0 kB
Private_Dirty:         4 kB
```

数据页在两边都是私有的一份:它是要写的,加载的时候动态链接器就往里改过地址(改的是什么,咱们下一篇里见),往后程序对它的读写也不会停,各改各的。代码页是没人写它的,内核就让所有用它的进程都指到了同一份物理页上。链接器那篇的答案到这里就齐了:全机器的进程,映射 libc 的都是同一份文件,它的代码页在物理内存里只住一份。用到 libc 的程序到了一万个,内存里的页照旧只有一份,硬盘上的文件也只有一份。静态库落下的两个毛病,内存里的万份拷贝,升级的时候全得重链,前一个到这儿解了,后一个的解法马上看,只换 .so 的文件,可执行文件的字节一个都不用动:

```text
$ gcc -shared -fPIC -o libmyutil.so b_v2.c    # 同一接口,helper 改返回 52
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
62
```

咱们没重新编译 dyn_app,重新链接的活也没干,再跑拿到的就是新版的 52,10 加 52 的结果是 62:升级动态库换的就是一个文件,用过它的程序下次启动全体生效。这话成立的前提是接口没动。函数叫什么名字、收什么参数、返回什么,这些对外的约定合起来叫 ABI(Application Binary Interface、二进制接口,开卷那篇的路线图里给过全称),接口一旦变了,换上去的库就跟旧程序对不上了,那是 ABI 那篇整篇的正题。

## CMake 视角

手敲 -shared 与 -fPIC 的这一套,日常的构建里都归 CMake 管。咱们把静态库那篇的静态版工程拿来,差的地方就这么几处:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myutil_dyn C)
add_library(myutil SHARED b.c)
set_target_properties(myutil PROPERTIES VERSION 1.2.3 SOVERSION 1)
add_executable(app a.c)
target_link_libraries(app PRIVATE myutil)
```

STATIC 换成了 SHARED。清单里只留了 b.c,本篇的演示用不着第二个成员,它还在静态库那篇的工程里。多的那行属性咱们一会儿看。CMake 替咱们带了什么,带上 --verbose 就看得清了:

```text
$ cmake --build build --verbose 2>&1 | grep -E "\-fPIC"
/usr/sbin/cc -fPIC -shared -Wl,--dependency-file=CMakeFiles/myutil.dir/link.d -Wl,-soname,libmyutil.so.1 -o libmyutil.so.1.2.3 CMakeFiles/myutil.dir/b.c.o
```

被笔者裁掉的编译那一行同样带着 -fPIC。留下来的链接行里 -fPIC 与 -shared 都在,咱们手敲的两样它全带了,还多干了一样:`-Wl,-soname,libmyutil.so.1`,给库把正式的名字登记了进去。产物落成了三件套,咱们用 ls 看一眼:

```text
$ ls -l build/libmyutil.so*
lrwxrwxrwx 1 charliechen charliechen    14 Sep 30 15:53 build/libmyutil.so -> libmyutil.so.1
lrwxrwxrwx 1 charliechen charliechen    18 Sep 30 15:53 build/libmyutil.so.1 -> libmyutil.so.1.2.3
-rwxr-xr-x 1 charliechen charliechen 15176 Sep 30 15:53 build/libmyutil.so.1.2.3
```

真身是带全版本号的 libmyutil.so.1.2.3,前面的两级是软链接,咱们挨个认过去。VERSION 1.2.3 记的是完整版本,SOVERSION 1 记的是接口代次,链接器把它拼进了 SONAME(soname 是 shared object name、共享对象名,也就是库给自己登记的正式名):

```text
$ readelf -d build/libmyutil.so.1.2.3 | grep SONAME
  0x000000000000000e (SONAME)             Library soname: [libmyutil.so.1]
$ readelf -d build/app | grep -E "NEEDED|RUNPATH"
 0x0000000000000001 (NEEDED)             Shared library: [libmyutil.so.1]
 (app 依赖 libc 的那行笔者裁了)
 0x000000000000001d (RUNPATH)            Library runpath: [/tmp/dynlib_demo/build]
```

app 的 NEEDED 记的是 libmyutil.so.1:它不记开发期的 libmyutil.so,也不记具体的 libmyutil.so.1.2.3,记下的正是接口代次的那一档。好处您看得见:哪天出了 1.2.4,只要换掉真身、把软链改了,app 的一行都不用重链。三件套怎么摆、版本号怎么定,细节的事归库搜索那篇和 ABI 那篇。RUNPATH 的那行,是 CMake 顺手写进的构建目录搜索路径,所以咱们的 ./build/app 不用 LD_LIBRARY_PATH 就跑得起来。咱们跑了,拿到的还是 42。还有个全局的开关 BUILD_SHARED_LIBS,咱们把它设成 ON 之后,add_library 不写类型的时候,默认就从 STATIC 翻成了 SHARED,咱们实测的产物是 libmyutil.so。不过全局的开关是一刀切,精度不如逐目标明写类型的做法,工程里通常还是明写的。

## 那行 call 里写的是什么

两阶段都走完了,咱们还剩最后一处没看:登记解决的是要哪个库,没解决的是地址是多少。a.o 里 main 调 helper 的那行 call,链接器那篇里咱们看着链接器在静态链接时把偏移当场填好了。眼下的 dyn_app 里,那个位置填上的会是什么?readelf -r 早把线索露给咱们了:

```text
$ readelf -r dyn_app | grep -E "COPY|JUMP"
000000004018  000700000005 R_X86_64_COPY     0000000000004018 shared_seed + 0
000000004000  000500000007 R_X86_64_JUMP_SLO 0000000000000000 helper + 0
```

这些列您全认识,目标文件解剖那篇里咱们逐列读过:Offset 说的是改哪儿,Sym. Name 说的是找谁,Addend 的角色是修正项,JUMP_SLO 是 readelf 列宽截断的 JUMP_SLOT。变了的只有一处:眼下两条重定位的填法,链接器自己是给不了的,地址的存在要等库映射了进来,填表的人也换成了运行期的动态链接器。机器码里那个位置究竟写的什么,代码怎么做到映射到哪个地址都能跑,接地址走的是什么表,下一篇整篇讲的就是这一件事。libmyutil.so 您接着留着,那篇用的还是它。
