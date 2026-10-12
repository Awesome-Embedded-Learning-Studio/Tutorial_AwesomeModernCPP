---
chapter: 13
difficulty: advanced
order: 9
platform: host
reading_time_minutes: 20
tags:
- cpp-modern
- host
- advanced
- 工具链
- 链接器
title: "位置无关与 GOT/PLT:地址加载时才定"
description: "非 PIC 编 .so 被链接器拦下的真实报错,x86-64 上 mov 过 GOT、call 过 PLT 的逐条反汇编,GLOB_DAT 与 JUMP_SLOT 两类动态重定位,懒绑定拿 LD_DEBUG 与 gdb 前后对拍,LD_PRELOAD 把库内部调用顶掉的真实输出,再捎上可执行文件侧的 COPY 重定位,最后收在 CMake 的 POSITION_INDEPENDENT_CODE。"
cpp_standard: [11, 14, 17, 20]
---

# 位置无关与 GOT/PLT:地址加载时才定

08 的结尾咱们留了一个口子:动态库的代码映射进进程,可执行文件里那行 call 写的到底是什么,当时咱们按下没讲。这个口子的来路还有前史。01 的 hello.s 里 call 的操作数尾巴上挂过一个 @PLT 后缀,file 又报过咱们编出的程序是 pie,当时只留了一句 pie 是位置无关的意思。05 的后段也点过名:pie 程序的地址是链接期定下来的,加载的时候还会整体挪动,机制留给了 09。今天咱们把这些一笔一笔兑现。

开头咱们把问题立清楚。一个共享库会被加载到哪个地址,构建的时候没人知道。可机器码偏偏离不开地址:咱们取数据要地址,调函数也要地址。编译器和链接器必须在不知道地址的前提下把指令写出来,再让加载的那一刻有人把真地址补进去。今天整篇就看两件事:写法长什么样,补的人怎么补。

照例交代环境:笔者的实验都在 WSL2 里跑,工具链是 GCC 16.2.1 配 binutils 2.47 加 glibc 2.44 加 CMake 4.4.3 的组合,这一篇里的每一段输出都是真实跑出来的。您那边版本不同,地址、路径这类细节可能有出入,机制本身倒是多年没变。

## 整体挪动:两个进程,两套地址

08 让咱们留着的 libmyutil.so,这一回咱们不用它。要看清 GOT/PLT 的机关,咱们换一个更瘦的三行标本,外加一个使用它的程序:

```c
// libcalc.c —— 导出一个变量和一个函数,再让另一个函数用它们
int tax_rate = 17;

int base_tax(void) { return 100; }

int total_tax(void) { return base_tax() + tax_rate; }
```

main.c 那边干的第一件事是打一行 start,接着才轮到 base_tax 和 total_tax 的调用。构建库的命令里带了个 -fPIC,它是做什么的、为什么非带不可,咱们下一段马上讲,这里咱们这就把程序跑起来,亲眼看 05 说的整体挪动:

```text
$ gcc -fPIC -shared libcalc.c -o libcalc.so && gcc main.c -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
start
base=100 total=117
```

(咱们带的 LD_LIBRARY_PATH=. 是让动态链接器到当前目录找库的,搜索顺序的完整规则到 10(库搜索逻辑)再展开。)再备一个同款的 app_wait:打完结果后停 30 秒,给咱们留观察窗口。咱们同时起两个它,翻出各自的 /proc/PID/maps:

```text
--- P1
5b08b67a6000-5b08b67a7000 r--p 00000000 00:49 103708  …/pic/app_wait
7773306ff000-777330700000 r--p 00000000 00:49 103684  …/pic/libcalc.so
--- P2
5a1c3db5c000-5a1c3db5d000 r--p 00000000 00:49 103708  …/pic/app_wait
78897cfa7000-78897cfa8000 r--p 00000000 00:49 103684  …/pic/libcalc.so
```

(咱们两行一组地看,长路径笔者裁短了,数字是原样的。)每组的头一行是程序自己:第二行是库。maps 里每行末尾的 inode 编号告诉咱们,P1 和 P2 映射的是同一个 app_wait 文件,libcalc.so 映射的也是同一个文件。地址呢?程序的两处在 5b08… 和 5a1c…,库的两处在 7773… 和 7889…。咱们眼前是同一个文件、两个进程,得到的却是两套地址。

笔者没有特意布置,这是内核在每次加载时主动打乱的结果,背后是一套叫 ASLR 的机制,全称是地址空间布局随机化(Address Space Layout Randomization),咱们系统里 /proc/sys/kernel/randomize_va_space 的值是 2,也就是完全的随机化。它再加上多个库不能挤同一个地址的事实,把答案摆在了咱们面前:库的加载地址,构建期给不了。

那机器码怎么办?最省事的写法是把地址直接写进指令,可地址是每次加载都会换的,写死就错了。有人会想:加载完由动态链接器把代码里的地址逐条改掉,行不行?当然不行。代码段在物理内存里是被多个进程共用的,08 里咱们看过它只住一份,谁都改出自己的一份,共用也就没了。剩下的路只有一条:让写出来的指令里根本不出现绝对地址。位置无关(Position-Independent Code)说的就是它,PIC 是它常用的缩写。

## -fPIC:写死地址的写法,链接器直接拦下

位置无关不是喊出来的。它是编译器一条条指令改出来的,-fPIC 就是干这个的开关。咱们把它摘掉试试,看看写死地址会落个什么下场:

```text
$ gcc -shared libcounter.c -o libcounter_nopic.so
/usr/bin/ld: /tmp/ccF4K1RU.o: warning: relocation against `counter' in read-only section `.text'
/usr/bin/ld: /tmp/ccF4K1RU.o: relocation R_X86_64_PC32 against symbol `counter' can not be used when making a shared object; recompile with -fPIC
/usr/bin/ld: final link failed: bad value
collect2: error: ld returned 1 exit status
```

(libcounter.c 咱们写得跟 libcalc.c 同款,也是一个导出变量加一个读它的函数,临时文件名 ccF4K1RU.o 是每次都换的。)这是实打实的报错,链接当场就失败了,连产物都没有落地。旧教材里常有“只是一条重定位警告”的说法,在咱们这套 GCC 16.2.1 上不成立。什么被拦了?咱们看编译器在非 PIC 下生成的取数指令:

```text
$ objdump -dr --no-show-raw-insn -M intel libcounter.o
0000000000000000 <get_counter>:
   0:	push   rbp
   1:	mov    rbp,rsp
   4:	mov    eax,DWORD PTR [rip+0x0]        # a <get_counter+0xa>
			6: R_X86_64_PC32	counter-0x4
   a:	pop    rbp
   b:	ret
```

咱们看到的是一条直取 counter 的 mov。按 04 里咱们认过的 rip 相对寻址,它写的是变量离它有多远。这段距离写死了,程序就等于断言了 counter 会一辈子待在编译它的那个模块里。可它偏偏是导出符号,运行时可能被别的模块顶掉,顶掉是什么样子,咱们到后面讲符号介入(interposition)的专节实际跑一遍。咱们回头想就知道,链接器索性把直取的路封了:R_X86_64_PC32 不许用在共享库里可顶替的符号上,报错里那句 recompile with -fPIC 说的就是换写法。

咱们顺带交代两个例外,它们都链接通过了,恰好反证了上面卡住的是什么。变量挂了 static 的,链接一次就过了,私有符号没有被顶的资格。只调用外部函数、不碰导出变量的库也链接通过,链接器给调用建了 PLT 条目(PLT 是过程链接表的缩写,咱们马上就讲)。工程里更容易遇上的是 .a 的场景:您拿了别人没带 -fPIC 编的静态库,就是 06 里咱们用 ar 打包出来的 .a 文件,想链进自己的 .so,报出来的错与上面的同款,主角换成了 .a 里的成员。

可执行文件这边咱们再看一眼,另有一档叫 -fPIE 的开关,Position-Independent Executable 是它的全称,说的是位置无关可执行文件。01 里 file 报过的那个 pie,今天咱们兑现它的身份:

```text
$ readelf -h app | grep Type; readelf -h app_nopie | grep Type; readelf -h libcalc.so | grep Type
  Type:                              DYN (Position-Independent Executable file)
  Type:                              EXEC (Executable file)
  Type:                              DYN (Shared object file)
```

(app_nopie 是咱们加 -no-pie 编出来的,其余的命令同款。)两档的分工就差在要不要防被顶:库用 -fPIC,它的导出符号随时可能被顶,所有跨模块的引用都得留出被顶的余地。可执行文件用的是 -fPIE,进程里它排在符号查找的头一位,自家定义的符号不会被别人顶掉,要防的只有自己被挪这一件事。您再看 Type 那列:pie 程序和 .so 一样都是 DYN,它们都是按装到哪儿都行的方式造出来的,固定地址的老式可执行文件用的才是 EXEC。

## GOT:数据的地址,从表里取

现在咱们拆正主。total_tax 的反汇编在手,咱们头一个看取数的部分:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so(节选)
0000000000001114 <total_tax>:
    1114:	push   rbp
    1115:	mov    rbp,rsp
    1118:	call   1030 <base_tax@plt>
    111d:	mov    rdx,QWORD PTR [rip+0x2eac]        # 3fd0 <tax_rate@@Base-0x40>
    1124:	mov    edx,DWORD PTR [rdx]
    1126:	add    eax,edx
    1128:	pop    rbp
    1129:	ret
```

读 tax_rate 用了两条 mov,咱们挨条看。头一条是 rip 相对的,注释里的 3fd0 才是要点,这个地址落在一张叫 GOT 的表里。GOT 的全称是全局偏移表(Global Offset Table),01 篇末“这一卷的十六篇”的清单报过它的名字。它取的东西不是 tax_rate 的值。它要的是表里 0x3fd0 那个槽存的内容,也就是 tax_rate 的运行时地址。第二条 mov 拿着这个到手的地址,才取到了真值 17。代码里没有任何写死的地址,写死的只有离 GOT 槽多远。GOT 长在库的身上,库整体挪动的时候它跟着挪,所以这段距离不变。

那这个槽里的地址是谁登记的,又由谁来填?咱们请出 04 的老朋友 readelf -r。输出咱们筛过了,只留下咱们关心的名字,完整表里另有几条链接器替启动代码登记的杂项:

```text
$ readelf -rW libcalc.so | grep -E 'tax_rate|base_tax'
0000000000003fd0  0000000500000006 R_X86_64_GLOB_DAT      0000000000004010 tax_rate + 0
0000000000004000  0000000700000007 R_X86_64_JUMP_SLOT     0000000000001109 base_tax + 0
```

0x3fd0 那行正是 tax_rate 的登记:类型是 R_X86_64_GLOB_DAT,意思是加载的时候,把 tax_rate 的真地址填进 0x3fd0 这个槽。04 里咱们读过的重定位是 PC32 和 PLT32,那批空位链接期就填掉了。GLOB_DAT 是重定位的另一半:它留给了动态链接器,填的时机在运行期。这就是地址加载时才定在文件里的形态。

数据的取法还有第三种,出现在可执行文件的那一头,咱们顺路看了。main.c 只调了函数,笔者另写了个 ratemain.c,整篇就一句直接读 tax_rate 的 printf,链出来的程序叫 rateapp:

```text
$ LD_LIBRARY_PATH=. ./rateapp
rate=17
$ readelf -rW rateapp | grep COPY
0000000000004018  0000000700000005 R_X86_64_COPY          0000000000004018 tax_rate + 0
$ nm rateapp | grep tax_rate; nm libcalc.so | grep tax_rate
0000000000004018 B tax_rate
0000000000004010 D tax_rate
```

咱们看类型:R_X86_64_COPY,拷贝重定位说的就是它。08 的结尾亮过一条 R_X86_64_COPY(shared_seed),干的就是这件事,shared_seed 落在 bss 的蹊跷,咱们在这一段里就能对上答案。nm 的两行最有意思:库里的 tax_rate 是 D,住的是 .data,初值 17 是它给的。rateapp 里凭空多出来了一个 tax_rate,它拿的字母是 B,照 04 的字母表,B 管的是 .bss。加载的时候,初值被动态链接器整个拷进了可执行文件的 .bss 副本,拷贝的凭据就是 COPY 记录。拷完了之后,两边用的是同一份,等下面咱们拿 LD_DEBUG 拍绑定记录,就能看到 libcalc 自己 GOT 里的 tax_rate 槽,填的就是可执行文件里复制出来的地址。可执行文件取它倒是痛快:一条 rip 相对的直取,连 GOT 的边都不沾。为什么能这么省?那个年代的可执行文件满是绝对寻址,链接器手里必须有确定的地址,拷一份进 .bss 就是唯一的选择。PIE 沿用了它:查找顺序里排头一位的正是可执行文件自己,直取自家的副本反而最便宜。代价是同一个变量在进程里有了两份记录,由此引出的问题归 12(ABI 设计)展开。

## PLT:函数的调用,多走一跳

读数的看完了,咱们回到 total_tax 里那行 call。01 的 hello.s 里,@PLT 挂在 call 的操作数尾巴上,当时只说它和动态库的调用方式有关,PLT 是跳板、GOT 是查地址用的表。等链接完成了,它长成了真实的实体。咱们看 app 这边的 main,app 也有自己的一套 PLT,咱们下面就看到:

```text
$ objdump -d --no-show-raw-insn -M intel app(节选)
    1180:	call   1060 <base_tax@plt>
    1188:	call   1050 <total_tax@plt>
```

call 的目标 0x1060,标号里写着的是 base_tax@plt。PLT 的全称是过程链接表(Procedure Linkage Table),04 里咱们远远望过它一眼,说它是函数调用走的跳板,今天咱们就把它的条目一条条读出来。每个条目是一小段固定的跳转代码,这就是行话里说的桩。app 和 libcalc 各养各的一套,各自伺候各自的调用点。libcalc 里的桩长成下面这样,连同它前面那个特殊的头一个条目:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so(节选)
    1020:	push   QWORD PTR [rip+0x2fca]        # 3ff0 <_GLOBAL_OFFSET_TABLE_+0x8>
    1026:	jmp    QWORD PTR [rip+0x2fcc]        # 3ff8 <_GLOBAL_OFFSET_TABLE_+0x10>
    102c:	nop    DWORD PTR [rax+0x0]
0000000000001030 <base_tax@plt>:
    1030:	jmp    QWORD PTR [rip+0x2fca]        # 4000 <base_tax@@Base+0x2ef7>
    1036:	push   0x0
    103b:	jmp    1020 <_init+0x20>
```

base_tax@plt 的桩一共三条指令,咱们挨个读。打头的 jmp,跳进 GOT 槽 0x4000 里存的地址,登记它的是 JUMP_SLOT,用法和 GLOB_DAT 是一样的,一个管的是函数,一个管的是数据。跟在后面的 push 0x0,压栈的是重定位序号,等于自报了家门:排 0 号的 base_tax。末尾的 jmp 1020,跳向 PLT 的头一个条目,也就是行话里的 PLT0。它干的是公共活:push GOT+8 槽,那里在加载时填了一张描述本模块的记录(link_map)。接着它把控制权交给了 GOT+16 槽,那里填着 glibc 的解析函数,咱们叫它 _dl_runtime_resolve。解析函数拿到栈上的序号,查出 base_tax 的真地址,把查到的地址回填进 0x4000 槽,这才把执行流送进了 base_tax 本尊。

那 0x4000 槽出厂时存的是什么?PLT 用到的那几个槽,单独住在一个叫 .got.plt 的节里,咱们把它的字节原样倒出来:

```text
$ readelf -x .got.plt libcalc.so

Hex dump of section '.got.plt':
 NOTE: This section has relocations against it, but these have NOT been applied to this dump.
  0x00003fe8 003e0000 00000000 00000000 00000000 .>..............
  0x00003ff8 00000000 00000000 36100000 00000000 ........6.......
```

咱们看表里的内容:一共四项,0x3fe8 存着动态段(.dynamic)的地址 0x3e00,08 里咱们拿 readelf -d 读 DT_NEEDED,读的就是它。0x3ff0 和 0x3ff8 是留给动态链接器的两个空位,link_map 和解析函数的值都是加载时才填的,所以文件里现在是两个 0。0x4000 按小端序读出来的值是 0x1036,恰好是桩里 push 0x0 指令的地址。0x4000 的出厂值把整件事串了起来:打头的 jmp 在出厂状态下转了一圈,又落回了自己桩里的 push 指令,接着走的是 push、跳进 PLT0、进解析函数。等解析函数把真地址写回了槽里,再调就从头一条 jmp 直接飞进 base_tax 了,解析的活一辈子只干一次。绕回自己再出发的出厂值,就是懒绑定全部的机关。

## 懒绑定:第一次调用才解析

JUMP_SLOT 的解析推迟到第一次调用,这样的做法叫懒绑定(lazy binding)。GLOB_DAT 倒是没这个待遇,加载期就处理完了。空口无凭的话咱们不认:得让动态链接器自己招供。咱们把环境变量 LD_DEBUG=bindings 设上,它就把每一笔符号绑定都记了下来。app 开头打了一行 start,然后才做头一次的调用,分界是现成的:

```text
$ LD_DEBUG=bindings LD_LIBRARY_PATH=. stdbuf -oL ./app 2>&1 | grep -E 'base_tax|tax_rate|start|base=' | grep -v stdbuf
   1578855:	binding file ./libcalc.so [0] to ./libcalc.so [0]: normal symbol `tax_rate'
   1578855:	binding file ./app [0] to /usr/lib/libc.so.6 [0]: normal symbol `__libc_start_main' [GLIBC_2.34]
start
   1578855:	binding file ./app [0] to ./libcalc.so [0]: normal symbol `base_tax'
   1578855:	binding file ./libcalc.so [0] to ./libcalc.so [0]: normal symbol `base_tax'
base=100 total=117
```

(行首的那串数字是进程号,您跑出来的不会一样。咱们用 stdbuf -oL 把 stdout 调成了行缓冲,不然的话管道里的 stderr 会抢跑,顺序就骗人了。)看分界:tax_rate 和 __libc_start_main 的绑定排在 start 前面,启动期的活都干完了。base_tax 的两条排在 start 后面,而且一条是 app 的,一条是 libcalc 的,正是两套 PLT 各自的头一次调用。咱们再换 LD_BIND_NOW=1 跑一遍,四条就全部提前到了 start 前面,懒与急的分野一目了然。

槽的前后值咱们也抓到了,这回咱们请 gdb 出场。咱们把断点一个安在 main 开头,另一个安在两笔调用返回的地方:gdb 默认把 ASLR 关掉了,库的加载基址才固定了下来,正好方便咱们读数:

```text
libcalc base = 0x7ffff7f9d000
before GOT slot = 0x7ffff7f9e036
   0x7ffff7f9e036 <base_tax@plt+6>:	push   $0x0
after  GOT slot = 0x7ffff7f9e109
   0x7ffff7f9e109 <base_tax>:	push   %rbp
```

头一次调用还没发生的时候,咱们读到的槽里存的是基址加 0x1036,指向桩内的 push 指令。而调用发生之后,槽里换成了 base_tax 本尊(基址加 0x1109),和重定位表里 Sym. Value 一列的 0x1109 正好对上。桩倒是没动过一个字节,变的只是 GOT 里的 8 个字节。

那默认到底是懒还是急?咱们不用记谁的说法,拿 readelf -d 一看便知:FLAGS 里带着 BIND_NOW 的就是急,不带的就是懒。在笔者的机器上,GCC 16.2.1 默认链出的可执行文件没有这个标志,走的是懒。咱们想主动选急,有两条路可走:链接的时候加 `-Wl,-z,now`,标志就直接进了文件。咱们也可以在运行前设 LD_BIND_NOW=1,换来一回临时的急。现在不少发行版和加固配置把急绑定设成了默认,启动的时候多花一点时间,换来的是所有解析都提前做完、攻击面小一圈,对服务端的程序常常是划算的。怎么选,咱们看的还是自己的程序更在乎启动的速度,还是运行期的平稳。

## 符号介入:库内部调用留了口子

咱们回到前面按下的问题:total_tax 调的是自家的 base_tax,凭什么也要过 PLT?上面的绑定记录其实已经把答案露出来了:libcalc 解析 base_tax 的时候,是把它当全局符号找的。动态链接器解析一个符号的时候,是把进程里所有模块的导出表摆开了从头找,排在前面的是谁,赢的就是谁,这里没有库总用自己人的特殊待遇。别的模块里的同名定义顶掉了库内部调用,这件事的行话叫符号介入(interposition)。咱们实际跑一回,咱们的顶替者只有一行:

```c
// libfake.c —— 顶替者:同名定义,返回值故意不同
int base_tax(void) { return 999; }
```

```text
$ LD_PRELOAD=$PWD/libfake.so LD_LIBRARY_PATH=. ./app
start
base=999 total=1016
```

LD_PRELOAD 让动态链接器把这个库排在所有库的最前面加载。对照开头的 base=100:999 的来历好懂,app 自己的调用被顶掉了。要紧的是 1016 = 999 + 17:total_tax 内部那声 base_tax(),调的也是顶替者。库自己写的调用被外人换了目标,咱们一行代码都不用重编。这其实不是事故。它正是机制的一部分:正因为库内部调用走 PLT 留了余地,替换才有了可能。您日后会见到的内存剖析、跟踪类工具,不少就是预载一份自己的 malloc、free 起家的,门道全在这儿了。这个口子开着倒是方便,可危险也是真的。谁排前面的完整规则,还有把口子收窄的手段——符号可见性,咱们到 11(符号可见性)专门讲。预载的库从哪儿被找到,则是 10(库搜索逻辑)一句话的事。

## CMake 视角

手敲 -fPIC 的机会如今不多,构建系统替咱们看着这一档。CMake 的总开关是 CMAKE_POSITION_INDEPENDENT_CODE,咱们的最小工程长这样:

```cmake
cmake_minimum_required(VERSION 3.20)
project(pic_demo C)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(calc STATIC libcalc.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
```

咱们拿 --verbose 把真实的编译命令翻出来对一对(路径咱们裁短了):calc 那边带上了 -fPIC,app 那边带的是 -fPIE,恰好就是本篇讲的两档。

```text
… -fPIC … -o CMakeFiles/calc.dir/libcalc.c.o …
… -fPIE … -o CMakeFiles/app.dir/main.c.o …
```

咱们想拨得更精细的时候,POSITION_INDEPENDENT_CODE 这个目标属性就派上了用场,咱们拿 set_target_properties 单独拨某一个目标,全局变量照顾不到的例外归它管。默认值咱们也实测了:SHARED 库自动带 -fPIC,一个字都不用您写。而 STATIC 库默认不带,什么时候疼?拿默认的静态库去链 .so 的那一刻,报的就是本篇开头咱们见过的 R_X86_64_PC32,报错句式与开头的同款,主角从 counter 换成了 .a 成员里的 tax_rate。所以拿静态库给人当 .so 原料的工程,全局开 PIC 是省心的起步配置。至于可执行文件的 PIE,您不用操心:app 的链接行上连 -pie 都没有,产物照样落成了 pie executable。默认的事归工具链,CMake 也就乐得不管了。

## 接下来:链接器按什么顺序找库

位置无关把装到哪儿都能跑这件事办成了:代码里不再嵌绝对地址,数据的出口开在了 GOT,函数的出口开在了 PLT,真地址加载期由动态链接器补上。05 里咱们看着 helper 在链接期拿到 0x112c,加载的时候又被整体挪动。挪完了之后,咱们看每一笔引用照样接得上,接的就是今天这些机关。

机器码这边算是通了,工程侧还有一个您天天会遇上的问题:链接的时候,链接器到底按什么顺序找库?-lcalc 找的是 libcalc.so 还是 libcalc.a?运行期 LD_LIBRARY_PATH、系统目录这些又各排第几?这些规则的完整展开,就是下一篇 10(库搜索逻辑)要讲的。
