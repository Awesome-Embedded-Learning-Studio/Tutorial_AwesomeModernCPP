---
chapter: 13
difficulty: advanced
order: 15
platform: host
reading_time_minutes: 15
tags:
- cpp-modern
- host
- advanced
- 工具链
- 链接器
title: "LTO 与现代链接器:优化的边界挪到链接期"
description: "编译加一个 -flto,优化从编译器内部挪进链接期:两个翻译单元的 scale 调用,-O2 下留着 call,-flto -O2 下折成 mov $0x2b 的立即数,函数连体带符号整体消失;readelf 解剖 slim 对象,.text 零字节、函数本体住进 .gnu.lto_* 节的中间表示,-ffat-lto-objects 的胖对象对拍;-v 输出实拍 collect2 常年递 liblto_plugin、lto1 的 -fwpa 与 -fltrans 两轮;ar 与 gcc-ar 索引一字不差、裸 ar 库不带 -flto 也链通的本机实情;-Wodr 在链接期点名 struct Config 两副面孔,inline 那一半依然沉默;41 文件链式标本 text 3377 缩到 1437、40 个 step 函数清零,链接时间翻倍的代价与增量构建省不下的链接开销;bfd 对 gold 真跑对拍(-print-prog-name 验换法、3 倍计时、LTO 插件照吃),lld 22.1.8 与 mold 2.42.1 末代 C++ 版拉来真跑;CMake 3.9 的 IPO 三个名字与 3.29 的 LINKER_TYPE 真跑,MOLD 无装的失败原样贴、PATH 一指即通;末尾 nm -D 证导出面不因 LTO 变动。"
cpp_standard: [11, 14, 17, 20]
---

# LTO 与现代链接器:优化的边界挪到链接期

上一篇走到收尾的时候,咱们留过一句挺绝对的判词:链接器干的,只是搬位置、接符号的活儿,除了重定位的那点改写,优化的事它一件不干。今天咱们就来亲手推翻它。推翻的办法只有一个开关,编译的时候加上 -flto 就行。这个做法的全名叫链接时优化(Link Time Optimization,咱们平时叫它 LTO),07 讲两副面孔的时候预告过一句,说它能让所有的翻译单元在链接时见面。今天咱们要看的就是见面这件事:从编译器交出去的东西,到见面之后的产物,咱们从头到尾走一遍。

见面为什么值钱,咱们拿旧知识垫一句。平常的编译器再聪明,一次也只看得见一个翻译单元:main.cpp 调用 util.cpp 里的函数,在编译 main.cpp 的那一刻,那个函数体长成什么样它根本不知道,能做的只有生成一条 call,再把对不上号的活儿留给链接器。03 里咱们对拍过 -O0 和 -O2 的函数体,改写的力度再大,也都发生在单个单元的内部。链接器倒是把所有的目标文件一次拿齐了,可它偏偏不懂优化,它的世界里只有符号和地址。LTO 动的正是这一步:编译器不再交机器码的成品,交的是中间表示(intermediate representation,咱们平时说的 IR)。链接器收到的东西换了,剩下的优化活儿,由它挪到链接的现场补办。

环境咱们照例交代:笔者的实验都在 WSL2 里跑,工具链用的是 GCC 16.2.1 加 binutils 2.47,glibc 到了 2.44,CMake 到了 4.4.3,这一篇里的每一段输出都是真实跑出来的。mold 和 lld 两位新式的链接器,今天也真跑了。笔者的机器到今天也没往系统里装它们,mold 拿的是官方的静态包,拉到 /tmp 解包就用了。lld 是从 Arch 的归档仓取的 22.1.8,配的是本机已有的 LLVM 22,一样给解在了 /tmp 里。后面的段落里,凡讲到它们俩的,摆出来的就都是真跑的输出。默认的链接器 bfd,和虽被判了弃用、binutils 2.47 里仍然随附的链接器 gold,都是真跑的。

## 跨单元内联:那条 call 是怎么消失的

标本咱们用最小的两个文件:

```cpp
// util.cpp:一个小函数,实现放在另一个翻译单元里
int scale(int v) { return v * 7 + 1; }
```

```cpp
// main.cpp:调用住在 util.cpp 里的 scale
#include <cstdio>
int scale(int);
int main() { std::printf("%d\n", scale(6)); }
```

咱们按老路子来,-O2 编了再链,再拿 objdump 翻开 main 给您看:

```text
$ g++ -O2 -c util.cpp -o util_o2.o && g++ -O2 -c main.cpp -o main_o2.o
$ g++ util_o2.o main_o2.o -o app_o2
$ objdump -d app_o2
    1044:	bf 06 00 00 00       	mov    $0x6,%edi
    1049:	e8 22 01 00 00       	call   1170 <_Z5scalei>
    104e:	48 8d 3d af 0f 00 00 	lea    0xfaf(%rip),%rdi
    1055:	89 c6                	mov    %eax,%esi
    1057:	31 c0                	xor    %eax,%eax
    1059:	e8 d2 ff ff ff       	call   1030 <printf@plt>
```

(开头收尾的那几条,笔者裁了。)这就是咱们看了十几篇的世界:参数 6 装进了 %edi,call 指名道姓地叫 _Z5scalei,结果再交到 printf 的手里。-O2 的档位再高,也救不了它,因为编译 main.cpp 的时候,它看不见 scale 的函数体。咱们现在给编译和链接都挂上 -flto:

```text
$ g++ -O2 -flto -c util.cpp -o util_lto.o && g++ -O2 -flto -c main.cpp -o main_lto.o
$ g++ -O2 -flto util_lto.o main_lto.o -o app_lto
$ objdump -d app_lto
    1044:	be 2b 00 00 00       	mov    $0x2b,%esi
    1049:	48 8d 3d b4 0f 00 00 	lea    0xfb4(%rip),%rdi
    1050:	31 c0                	xor    %eax,%eax
    1052:	e8 d9 ff ff ff       	call   1030 <printf@plt>
```

(开头收尾的那几条,笔者同样裁了。)call 没了,printf 的第二个参数直接装上了 0x2b。十六进制的 2b 就是 43,正是 scale(6) 的答案:6 乘 7 再加 1 的结果,在链接的现场就算完了,折成了一条指令里的立即数。后面的发现更有意思,咱们在整个 app_lto 的反汇编里搜 _Z5scalei,一条都搜不到了。它连函数体带符号整个没了:内联之后的它,再没有任何人会引用了。程序跑起来照样打印的是 43,语义上的一分一毫都没动。

值得咱们看清楚的是,这两招您都不是头一回见:常量折算您在 03 的 -O2 里亲眼见过,内联的名字 03 里也点过。新鲜的地方其实在视野:以前的优化圈在单个单元的内部,现在全程序的函数体,都进到了同一场优化里。

## 链接器拿到手的,不是机器码

您看,call 能被吃掉的前提,是链接的那一刻,有人看得见了函数体。咱们把 -flto 编出来的 util_lto.o 拿去解剖,看看编译器到底交了什么:

```text
$ readelf -SW util_lto.o
  [ 1] .text             PROGBITS        0000000000000000 000040 000000 00  AX   0   0   1
  [12] .gnu.lto__Z5scalei.0.6703735e1136341f PROGBITS        0000000000000000 000122 0000f8 00   E  0   0   1
  [18] .gnu.lto_.opts    PROGBITS        0000000000000000 0004af 00006b 00   E  0   0   1
  [22] .symtab           SYMTAB          0000000000000000 000568 000048 18     23   2  8
```

(.text 和 .gnu.lto_.opts 之间还夹着十几个 .gnu.lto_ 开头的节,笔者裁了。)两条大新闻都在上面的几行里。头一个发现是 .text 的大小:000000,一个字节的机器码都没有。函数本体住进了 .gnu.lto_ 开头的节里,有一条节名里就写着 _Z5scalei 的名字,里面装的是 GCC 的中间表示(GCC 家的 IR 叫 GIMPLE,语法咱们不学,认得它不是机器码就够了)。您还会发现 .symtab 也没走,nm 照样报出 00000000 T _Z5scalei 的条目,只是它的地址是 0。符号表是健在的,05 里咱们练过的符号解析照旧转得动,变的只是名字背后挂的东西。

GNU ld 自己是看不懂 .gnu.lto_ 节的,中间的翻译得有人做,出面干这活的就是插件(plugin),它跟 13 里 dlopen 一类的运行期插件不是一回事。咱们拿 -v 翻开链接命令,交接的过程就摆在眼前:

```text
$ g++ -O2 -flto util_lto.o main_lto.o -o app -v
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 -plugin /usr/lib/gcc/x86_64-pc-linux-gnu/16/liblto_plugin.so \
   -plugin-opt=/usr/lib/gcc/x86_64-pc-linux-gnu/16/lto-wrapper ...(后面一长串,笔者裁了)
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/lto1 -quiet -dumpbase ./app.wpa ... -fwpa -fresolution=/tmp/ccdZwZ4R.res ...
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/lto1 -quiet -dumpbase ./app.ltrans0.ltrans ... -fltrans @/tmp/ccX1B1sV -o /tmp/cc6ZjdaB.s
```

(临时文件的名字,笔者原样保留了,您自己跑一遍拿到的会是另一串,不影响咱们看事。)collect2 把 liblto_plugin.so 递给了链接器,插件把 lto-wrapper 请了出来,lto-wrapper 又把编译器本体 lto1 拉起来跑了两轮。头一轮 -fwpa 干的是全程序的分析,内联的决断在这一轮做完。第二轮 -fltrans 把分析完的分块翻回了真正的机器码。中间那个 -fresolution 文件也有来头:链接器把自己做好的符号决议写了进去,谁和谁归并了,哪个定义活下来了,全写在了单子里,编译器照着单子干它的活。您看,优化的活儿链接器确实一件没碰,可它把整条的优化流水线请进了门,上一篇的判词,到这儿就算翻案了。

这类只装 IR 的目标文件有个名字,咱们叫它 slim 对象,纤薄的对象。两样都装是做得到的,咱们得显式加 -ffat-lto-objects,产物的名字是胖对象。把三份文件摆在一起看大小:普通的 util_o2.o 是 1224 字节,slim 的 util_lto.o 反而是 3680,胖的 util_fat.o 是 3920。slim 的居然比普通的还大,您没看错:函数太小了,IR 自带的登记信息占了上风。(slim 与胖的字节数、.gnu.lto_ 节名尾巴上的十六进制,每次重跑出来的都会有几个字节的出入,IR 的登记号带着随机性。不变的是次序:普通的最小,slim 的居中,胖的最大。)纤薄说的是内容物里没有成品的机器码,说的不是文件的个头。咱们把胖对象翻开,.gnu.lto_ 的节全都在,.text 里另有货真价实的 13 个字节,那是给读不了 IR 的场合预备的真码。咱们卷里的常规构建用不上它,CMake 甚至会替咱们指名要 slim 的,这个到 CMake 一节您亲眼看。

静态库的这一头还有一段衔接。06 里咱们验过,binutils 2.47 的 ar 打新档案,连 rc 都默认把符号的索引写上了。LTO 的对象进了档案,索引该谁写?GCC 备了自家的三兄弟:gcc-ar、gcc-nm、gcc-ranlib,干的活跟原名一样,不同的是它们带着插件进档案,把 IR 里的符号也点进了索引。咱们拿 slim 的对象真造了一对档案:

```text
$ ar rc libplain.a util_lto.o && gcc-ar rc libgccar.a util_lto.o
$ nm -s libplain.a
Archive index:
_Z5scalei in util_lto.o
$ nm -s libgccar.a
Archive index:
_Z5scalei in util_lto.o
```

(两边成员列举的两行,笔者都裁了。)咱们对了两边,结果是一字不差,倒也不奇怪:slim 对象的符号同时登在了 ELF 符号表里,裸 ar 照着符号表写的索引,把该记的都记全了。真正的意外在链接这边,咱们拿裸 ar 打的库去链接,链接的命令行上不带 -flto:

```text
$ g++ -O2 main_o2.o libplain.a -o app_nolto && ./app_nolto
43
```

链接居然就这么过了,还打印了 43。回头看 -v 才明白:这套 GCC 16 的驱动,把 -plugin 递给 collect2 是常年的动作,链接的命令行上有没有 -flto,它递的都是同一个。所以一些老资料里 slim 库没 -flto 就链不动的说法,在这套工具链上不成立:插件照旧认领了 IR,现场把机器码补了出来。咱们翻 app_nolto 的反汇编,scale 的函数体在,lea、sub、add 的三条,v*8-v+1 的写法,是 -O2 重新生成的。gcc-ar 的用武之地,在裸 ar 看不见符号的场合:符号只有插件读得出的时候,裸的索引就会漏写。咱们的小标本造不出更刁钻的局面,知道边界在哪儿就行了,别的工具链什么脾气,以您手上的 -v 输出为准。

## 两副面孔,-Wodr 这回喊了

见面带来的还有一层副产品。07 里咱们验过两个判例。类型长两副面孔的,-Wodr 就抓得住了,它是 GCC 专门用来报 ODR 违规的警告开关,ODR 的全称是 One Definition Rule、单一定义规则,LTO 一开它当场就点了名。inline 函数长两副函数体的,咱们挂上 -flto -Wall -Wextra,它的表现是一声不吭。重跑头一个的成本很低,咱们拿它做一遍对照。咱们在 config.h 里让宏剪出两副面孔:

```cpp
// config.h(外面包着头文件守卫)
struct Config {
    int limit;
#ifdef FLIP
    char tag;
#else
    long tag;
#endif
};
```

咱们把一个单元照旧编,另一个编的时候带上 -DFLIP,最后咱们拿 -flto -Wall -Wextra 链起来:

```text
$ g++ -O2 -flto -Wall -Wextra old_cfg.o new_cfg.o cfg_main.o -o cfg_app
config.h:2:8: warning: type 'struct Config' violates the C++ One Definition Rule [-Wodr]
    2 | struct Config {
      |        ^
config.h:2:8: note: a different type is defined in another translation unit
    2 | struct Config {
      |        ^
config.h:7:10: note: the first difference of corresponding definitions is field 'tag'
    7 |     long tag;
      |          ^
```

(往下笔者还裁了两段 note,它们报的是另一副面孔里的同名字段和类型不匹配。)这几行是链接那一步吐的:咱们把三个 .o 单独各编了一遍,得到的全是零输出,警告只在最后集合的时候出现。道理其实不深:平常的两个单元互不相见,谁的 Config 都合法。LTO 让它们碰了头,同名不同体的差异,才终于有工具看得见了。对照组咱们也真跑了,同样的两副面孔,咱们不带 -flto 编一遍链一遍,它的表现还是一声不吭,程序照常跑出了 16 0。当然,标准对两副面孔程序的判罚没有变,它的身份是未定义行为,今天警告抓到了算咱们赚的,抓不到它也不欠咱们的解释。至于 inline 的那一半,07 里咱们验过它依然沉默,所以防线不在工具上:同一个实体的定义,只从同一份的头文件里拿。

## 四十个单元,能省出多少

两个文件的小标本上,咱们能看见的收益很小:app_o2 的 text 是 1505,app_lto 的 text 是 1437。想看 LTO 的力气,咱们得把跨单元的边界铺开。笔者生成了一个链式的标本:40 个 chain_i.cpp,每个 step_i 干的都是点小循环,然后调用下一个单元的 step_{i+1},main 只调了 step_0。每个函数都只被调用了一次,这样的条件对内联格外友好,咱们把话讲在前头,您回头拿真工程的数字来对质,就别拿它当基准了。四十一个目标连成的是同一条链,咱们让 -O2 对上 -O2 -flto:

```text
$ size app_o2 app_lto
   text	   data	    bss	    dec	    hex	filename
   3377	    632	      8	   4017	    fb1	app_o2
   1437	    632	      8	   2077	    81d	app_lto
$ nm app_o2 | grep -c "T _Z[0-9]*step"
40
$ nm app_lto | grep -c "T _Z[0-9]*step"
0
```

40 个 step 的函数一个都没剩下,text 缩到了四成多。平常的链接器不敢动外部的函数,怕的就是名字被别人引用。-fresolution 的单子告诉了编译器:产物是最终的可执行文件,40 个名字没有任何外部的引用,编译器也就删得心安理得了。前面 scale 的整个消失,用的就是同一个机制。诚实的话咱们也要说全:小标本的差异是 4.5%,链式的标本是 57%,真工程落在两者的中间。LTO 是值得开的,可它给不了稳赚的承诺,单元边界越密、调用越深的地方,咱们才越有得挖。

## 代价:链接那一头变贵了

收益是有价的,代价落在了链接的那一头。同一份 41 文件的标本,咱们用 shell 的 time 各计时三轮,普通的链接是 0.032、0.035、0.032 秒,-flto 的链接是 0.067、0.067、0.071 秒。翻倍了,但绝对值是毫秒级的,咱们不夸大。真正要掂量它的,是规模大的工程,差距是会跟着规模一起长的。理由其实也直白:普通的链接搬的是符号和地址,LTO 的链接要把全程序的分析和代码生成重跑一遍。LTO 的代码生成支持并行,-flto=auto 就是把并行度交给机器的核数。咱们也真跑了三轮,拿到的是 0.076、0.069、0.067 秒,在这个标本上没赚到什么:41 份 IR 最后只切出了一个 ltrans 分块,就是 -v 里那个 .ltrans0.ltrans 的临时文件,并行的活儿本来就没几份,倒白付了启动并行的固定开销。

增量构建省不下来的位置更具体。咱们只改 chain_5.cpp 一个文件,重编它只花了 0.009 秒,增量的省,在编译的那一头还是成立的。可接着的 LTO 链接花了 0.063 秒,跟全量的花销是一个价:GCC 默认不给 LTO 留缓存,所以 41 份 IR 每次链接都从头来过,工程大了以后,链接的那头就越沉。所以开了 LTO 的项目,常见的做法是调试的构建把它关掉,发布的构建再打开。取舍咱们留到 CMake 一节,那边您一个开关就能切换。

## 换个链接器:-fuse-ld 与这几家人

讲了半天 LTO,该轮到链接器本身了。咱们平时嘴里的 ld,它的真身是 ld.bfd。bfd 是 binutils 家那层读 ELF 的库,它的全称是 Binary File Descriptor。同一家还有个为提速而生的 gold,门外有 LLVM 家的 lld,最新的,是 2020 年动工的 mold。换链接器的开关是 -fuse-ld=名字,驱动的做法很直白:按改过的名字找程序。这个咱们一条命令就能验:

```text
$ g++ -print-prog-name=ld
ld
$ g++ -fuse-ld=gold -print-prog-name=ld
ld.gold
```

gold 咱们真跑。它的来历一句话就够:binutils 家为了链接提速造的,主打的是多核的并行,只认 x86 一类的少数目标。官方已经把它判了弃用,binutils 到了 2.44,连源码包里都不再收它了,可笔者的 2.47 仍然随附,正好拿来给咱们对拍。标本还是咱们用过的 41 个目标,咱们还是各跑三轮,bfd 的成绩是 0.031、0.032、0.032 秒,gold 的成绩是 0.011、0.011、0.012 秒,小标本上快了三倍,毫秒级的绝对值咱们照实报。产物的个头也小了一截,18880 字节的 bfd 输给了 14840 字节的 gold。LTO 咱们也搬过去验了:把 -flto 的 41 个对象交给 gold,链成了,跑通了,结果也是同值的。这不算巧合:链接器的插件接口,最早就是 gold 实现的,当年的 LTO 就是踩着它起来的。

lld 和 mold 咱们今天也真跑了。lld 是 LLVM 家的链接器,也是 clang 工具链的默认搭档,咱们拿 -fuse-ld=lld 就能切过去,CMake 的 LINKER_TYPE LLD 也行。mold 是几家里最年轻的,作者当年也是 lld 的主力作者,目标就是把链接的速度做到最快。它 2026 年 9 月发布的 2.42.1,多半是它在 C++ 里的最后一个版本,作者正在把它重写成 Rust 的新版本,3.0 也排进了计划里。两位都是解在 /tmp 用的,lld 拿的是归档仓里配本机 LLVM 22 的 22.1.8,跑的时候咱们把目录临时指给 PATH,lld 那边还得用 LD_LIBRARY_PATH 把库的搜索路径也指上,这样就能跑起来了。标本还是咱们用过的那 41 个目标,咱们让四家同轮各跑三轮,数字是这一轮的,咱们报的是后两轮的稳态,冷启的头一轮搁在一边了:bfd 的成绩是 0.035、0.033 秒,gold 的成绩是 0.012、0.013 秒,mold 和 lld 的稳态分别落在 0.021、0.022 秒和 0.019、0.018 秒。您自己重跑,拿到手的数字就会随冷热有出入。这一轮最快的反而是 gold,不过四家的差距全在毫秒级,谁快谁慢在这个标本上说明不了太多。个头倒是拉开了,bfd 的 18880、gold 的 14840 之外,mold 排出了 13200,lld 排到了 10920,一样的 41 个目标,新一代的排得更紧,节的摆法、对齐的给法,各家有各家的排法。LTO 的那 41 份对象也搬了过去,两位都链通了,也跑通了,打出来的还是同值的 552886381。不过这还是小标本,计时是毫秒级的,个头的出入也就是几千个字节,真到了工程里,这些差距才真正地拉开。

## CMake 视角

LTO 在 CMake 那边走的是 IPO 的叫法,它的全称是跨过程优化(Interprocedural Optimization),LTO 是它在 GCC 上的具体形态。咱们要认识的名字有三个,都是 CMake 3.9 立起来的:全局的变量叫 CMAKE_INTERPROCEDURAL_OPTIMIZATION,目标一方的属性叫 INTERPROCEDURAL_OPTIMIZATION,还有名叫 CheckIPOSupported 的模块,咱们用它跑 check_ipo_supported() 检查。最小的工程长成了这样:

```cmake
cmake_minimum_required(VERSION 3.9)
project(ipodemo CXX)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_out)
if(ipo_ok)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
else()
  message(WARNING "IPO 不支持: ${ipo_out}")
endif()
add_executable(app main.cpp util.cpp)
```

check_ipo_supported 干的是真编真链一趟的探针,遇到不认 IPO 的编译器,您拿到的是 ipo_ok 的 FALSE 和 ipo_out 里的原因,而不是构建到一半才失败。策略 CMP0069 管的也是同一套行为,引入的年头也是 3.9,老工程升级要是冒出了它的警告,查的正是同一个地方。咱们构建一遍再拿 --verbose 翻开:

```text
$ cmake -S . -B build && cmake --build build --verbose
/usr/sbin/c++ -flto=auto -fno-fat-lto-objects -MD ... -c .../main.cpp
/usr/sbin/c++ -flto=auto -fno-fat-lto-objects ... main.cpp.o util.cpp.o -o app
```

(路径与依赖文件的那几段,笔者裁了。)两处值得您看:编译行和链接行都带上了 -flto=auto,咱们前面手动敲的 -flto,CMake 替咱们填到了两头。旁边的 -fno-fat-lto-objects,是 CMake 指名要 slim 的对象,跟咱们前面解剖的胖瘦对上了。挑链接器也有正路了,3.29 起的 LINKER_TYPE 目标属性,变量版的 CMAKE_LINKER_TYPE 管全工程:

```cmake
cmake_minimum_required(VERSION 3.29)
project(ldtype CXX)
add_executable(app_bfd main.cpp util.cpp)
set_property(TARGET app_bfd PROPERTY LINKER_TYPE BFD)
add_executable(app_gold main.cpp util.cpp)
set_property(TARGET app_gold PROPERTY LINKER_TYPE GOLD)
```

```text
$ cmake -S . -B build && cmake --build build --verbose
/usr/sbin/c++ -fuse-ld=bfd ... -o app_bfd
/usr/sbin/c++ -fuse-ld=gold ... -o app_gold
```

取值单上的成员有 BFD、GOLD、LLD、MOLD 这些,落到链接的命令行,还是咱们刚才用过的 -fuse-ld。MOLD 的一档笔者也真配了工程,构建真跑了,失败原样贴给您:

```text
$ cmake --build build --verbose
collect2: fatal error: cannot find 'ld'
/usr/sbin/c++ -fuse-ld=mold ... -o app
```

原因其实一点也不神秘:笔者的机器上没装 mold,collect2 找不着叫 ld.mold 的程序,当场就收了工。不过不装,咱们也照样能把它喂给 CMake,办法就是把解包出来的 mold 的 bin 临时加在 PATH 的最前头,构建目录还是原来的,命令也还是原来的那一行,咱们再跑一遍,链就通了,这样的喂法压根不用把 mold 装进系统:

```text
$ PATH=/tmp/mold-run/mold-2.42.1-x86_64-linux/bin:$PATH cmake --build build --verbose
/usr/sbin/c++ -fuse-ld=mold -Wl,--dependency-file=CMakeFiles/app.dir/link.d CMakeFiles/app.dir/main.cpp.o CMakeFiles/app.dir/util.cpp.o -o app
[100%] Built target app
$ ./build/app
43
```

调试关、发布开的切换,CMake 也有文档化的路数:变量 CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE 只开 Release 的一档,咱们把 Debug 与 Release 各配各链了一遍,发布档的链接行带着 -flto=auto,调试档的一条 -flto 都不带,两边跑出的都是 43。

## 导出面:LTO 一格都没动它

还有一件容易担心的事,咱们顺手按下:优化都看穿全程序了,导出的面会不会被它动?咱们按 11 的老办法,给 helper 标了 hidden,给 scale_one 标了 default,再拿 -flto -fvisibility=hidden 编成了动态库:

```text
$ nm -D libvis.so
00000000000010f0 T _Z9scale_onei
                 w __gmon_start__
```

(另三条运行时的弱钩子,笔者裁了。)咱们担心的事没有发生:动态符号表里只剩下了对外的 scale_one,hidden 的 helper 一格都没露。优化器在库的内部怎么折都行,哪些名字出得了门,还是 visibility 说了算。11 讲的版本脚本照旧管用,12 立下的 ABI 头四件套,也不因为挂了 -flto 就豁免。

## 接下来:番外

咱们这一卷的正文,到这一篇就收齐了。四个阶段、静态库与动态库、符号与可见性、ABI、程序的启动,再到今天把优化挪进了链接期,咱们一条线走下来,链接器从只管搬运的位置上,干起了半个编译器的活。剩下的一篇是番外,01 开卷立路线图的时候埋过它的题目:动态库,能不能当成可执行文件直接跑?08 里咱们还见过引子,libmyutil.so 头部登记的 Entry 是 0x0,等于明说了自己不打算跑。它到底能不能直接跑、硬跑了会发生什么,16(番外:动态库能当可执行文件跑吗)整篇玩的就是这个,咱们番外见。
