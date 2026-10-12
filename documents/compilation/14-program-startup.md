---
chapter: 13
difficulty: advanced
order: 14
platform: host
reading_time_minutes: 16
tags:
- cpp-modern
- host
- advanced
- 工具链
- 链接器
title: "程序的启动:从 exec 到 main"
description: "从一句 ./app 到 main 的第一行,中间的整段流程:strace 逐行追 execve 之后的系统调用,gdb 的 starti 实证第一条指令落在 ld.so、maps 里只有 exe 加 interpreter 两个文件,两阶段装载立住;readelf -l 正式读程序头表的 INTERP 与 LOAD;gdb 三断点走 _start、__libc_start_main、main 的次序,_start 十三条指令挑要紧的读,main 的地址走 lea 进 rdi,call 过 GOT 槽是 GLOB_DAT 加载期解析;glibc 2.44 上 init/fini 两参清零、__libc_csu_init 已删的新形态;collect2 链接行实拍 Scrt1/crti/crtbeginS/crtendS/crtn 一排的分工,_init 由 crti 头拼 crtn 尾、__gmon_start__ 弱钩子的落点;.init_array 里 frame_dummy 与 _GLOBAL__sub_I_ 两项实读;-static 对拍 19 行 trace 少掉整段第二阶段;收在 exit 半句与 CMake 的 pie 对 -no-pie(Type DYN 对 EXEC)。"
cpp_standard: [11, 14, 17, 20]
---

# 程序的启动:从 exec 到 main

上一篇结尾的地方,咱们把整条路预告过一遍:从咱们敲下的一句命令,到 main 的第一行执行,中间隔着的是装载、重定位、运行库初始化的一大段。今天这篇就是来兑现的。预告不重播了,咱们直接把工具架上,一步一步地真走。想看 main 之前发生的事,咱们的眼睛得贴到系统调用这一层:咱们请出 strace,它是一个专门跟踪系统调用的工具,哪个进程陷进了内核、调了什么,它都会原样地记下来。

咱们用的标本还是卷里的老面孔 libcalc:一个导出 int 变量加两个函数的动态库,配的是一个调用它的 app,跑它的时候咱们带着 LD_LIBRARY_PATH=.,让它在当前目录里找着要用的库。环境咱们照例交代:笔者的实验都在 WSL2 里跑,工具链用的是 GCC 16.2.1 配 binutils 2.47 加 glibc 2.44 加 CMake 4.4.3,strace 拿的是 7.2,gdb 拿的是 17.2,这一篇里的每一段输出都是真实跑出来的。您那边的版本不同,系统调用的名目、地址这类细节可能有出入,主线的步骤倒是多年没变。

## execve:头一行,进程换了个人

咱们把 app 架上 strace 跑了一遍,全部的输出拢共 51 行,咱们从头看起。头一行的内容是这样的:

```text
execve("./app", ["./app"], 0x7ffe681792c0 /* 70 vars */) = 0
```

头一行的 execve 是一个系统调用,三个参数分别是程序的路径、命令行参数的清单、环境变量的清单。它干的活,是把当前的进程整个换成磁盘上的这个程序:原来那个进程的代码、数据、栈,全都作废了。返回 0 的那一刻,shell 交出来的子进程已经不存在了,活着的是 app。上一篇的预告里点过它的名,今天咱们看的是真身。

剩下的 50 行里,有一件特别扎眼的事:您翻遍全文,找不出一条打开 ./app 或者 ld-linux 的调用。libcalc 和 libc 都有整段的 openat 与 mmap,exe 和动态链接器的本尊,倒是连一次 openat 都没轮上。这还真不是 strace 漏了记:内核在 execve 的内部就把头两个文件映射好了,这样的活在内核里就干完了,系统调用的记录里自然没有它们。您要是问,凭什么说内核装的就是头两个?空口的话咱们不认,下一节咱们就拿出证据。

## 两阶段:内核只装两个,剩下的归动态链接器

咱们请 gdb 帮个忙:它的 starti 命令,能把进程停在货真价实的第一条指令上,停的位置比 main 早,也比 04 里 Entry 指着的那个入口 _start 早。

```text
$ gdb -q -batch -ex starti ./app
Program stopped.
0x00007ffff7fdf7e0 in ?? () from /lib64/ld-linux-x86-64.so.2
```

(gdb 自己起跑的几行提示,笔者裁了。)停哪儿了?不在咱们的 app 里,落在了 /lib64/ld-linux-x86-64.so.2 里。您没看错,它就是动态链接器:01 的 file 输出末尾管它叫 interpreter,08 里咱们揭过它的真身。此刻咱们顺手看一眼进程的 maps:

```text
0x0000555555554000 0x0000555555555000 ... r--p  /tmp/.../lab/app
0x0000555555555000 0x0000555555556000 ... r-xp  /tmp/.../lab/app
0x00007ffff7fbe000 0x00007ffff7fbf000 ... r--p  /usr/lib/ld-linux-x86-64.so.2
0x00007ffff7fbf000 0x00007ffff7fec000 ... r-xp  /usr/lib/ld-linux-x86-64.so.2
```

(每行中间的 Size、Offset 几列笔者裁掉了,长路径也裁短了,地址是原样的。两个文件各取了头两段,四段里的后两段,笔者也一并裁了。路径您可能留意到:gdb 报的落在 /lib64 下,maps 记的却是 /usr/lib,两副路径指的是同一个文件,前者走的是链接,后者才是解析后的真身。)此刻的进程里,真身只有两个:app 的四段,加动态链接器的四段。剩下的 vvar、vdso 几小段,是内核的东西,08 里咱们点过 vdso 的名。咱们翻遍了,libcalc 的名字不在,libc 的名字也不在,它们的进场,得等动态链接器动了手。装载的两阶段,到这儿也就立住了:内核的阶段只认两个文件,一边是可执行文件的本尊,一边是它头部登记的 interpreter。NEEDED 单子上的其它库,没有一个是归内核管的。第二阶段是动态链接器在用户态跑的,从 trace 的第 2 行起,活就全是它的了,下面贴的,是它装 libcalc 的一段:

```text
openat(AT_FDCWD, "./libcalc.so", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0755, st_size=15360, ...}) = 0
mmap(NULL, 16408, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x7e9e759d1000
mmap(0x7e9e759d2000, 4096, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x1000) = 0x7e9e759d2000
mmap(0x7e9e759d3000, 4096, PROT_READ, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7e9e759d3000
mmap(0x7e9e759d4000, 8192, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x2000) = 0x7e9e759d4000
close(3)                                = 0
```

(找库时它还顺带多试了 glibc-hwcaps 一类的 CPU 特性子目录,都落了空,那两行笔者裁了。openat 与 fstat 之间的 read、getcwd 两行,也一并裁了。搜索次序的完整规则是 10 的内容,咱们只看现象。)后面四条 mmap 的地址一个挨一个,readelf -l 里四条 LOAD 的运行期形态,对的就是它们。程序头表(program header)今天算是正式出场了:04 里咱们拿 readelf -S 读的节表,是给链接器看的切法,程序头表给的则是装载的视角,一条 LOAD 对应的是一次映射。咱们把 app 的读出来:

```text
$ readelf -l app
Program Headers:
  Type           Offset             VirtAddr           PhysAddr
                 FileSiz            MemSiz              Flags  Align
  INTERP         0x00000000000003ac 0x00000000000003ac 0x00000000000003ac
      [Requesting program interpreter: /lib64/ld-linux-x86-64.so.2]
  LOAD           0x0000000000000000 0x0000000000000000 0x0000000000000000
  ...
  DYNAMIC        0x0000000000002dd0 0x0000000000003dd0 0x0000000000003dd0
  GNU_RELRO      0x0000000000002dc0 0x0000000000003dc0 0x0000000000003dc0
```

(完整的表有十几条,PHDR、四条 LOAD、几条 NOTE 都在笔者手里,裁掉的部分与主线无关,省略号是笔者加的。)INTERP 登记的就是动态链接器的路径,内核要装的第二个文件,就是从它这儿知道的。DYNAMIC 是 .dynamic 节的装载视角,08 里咱们拿 readelf -d 逐行读过,NEEDED 的单子就住在里面。GNU_RELRO 咱们多说半句:名字里的 RELRO 读作 relocation read-only,重定位做完改回只读的意思。trace 里靠后的那几条 mprotect,干的就是这个活,把写过 GOT 的那些页改回只读。09 里咱们看着动态链接器给 GOT 填表,等这几条 mprotect 跑完了,表里的值就都定下来,写过的页也回到了只读。

装完了 libcalc 再装 libc,流程是同款的,咱们不重复贴。全都装完了之后,trace 里还有一段 arch_prctl、set_tid_address、getrandom 的调用,那是动态链接器在做初始化:线程局部存储的基址,就备在这儿了。这些都跑完了,往后才是咱们熟悉的输出 start 与 base=100 total=117,一次 write 全送了出去。您看,咱们写的第一行输出,排在一长串准备工作的后面。

## _start:第一个属于程序的指令

库装完了、表填完了,才轮到咱们程序的入口。04 里咱们看 readelf -h 的时候,Entry 那行指向一个叫 _start 的位置,当时咱们说 main 之前的事归它管。今天咱们拿 nm 一对:Entry 的 0x1070,对应的就是 _start 的地址。

咱们请 gdb 出场,挂上了三个断点,把次序跑给您看:

```text
$ gdb -q -batch -x three_bp.gdb ./app        # 脚本:b _start / b __libc_start_main / b main / run / bt / c ...
Breakpoint 1 at 0x1070
Function "__libc_start_main" not defined.
Breakpoint 2 (__libc_start_main) pending.
Breakpoint 3 at 0x1172
...
Breakpoint 1, 0x0000555555555070 in _start ()
Breakpoint 2, 0x00007ffff7c2783c in __libc_start_main () from /usr/lib/libc.so.6
#1  0x0000555555555095 in _start ()
Breakpoint 3, 0x0000555555555172 in main ()
start
base=100 total=117
[Inferior 1 (process 30196) exited normally]
```

(省略号处是 gdb 的前导提示与线程库的提示行,各站 bt 的 #0 帧也裁了,咱们只留了第二站的 #1,招出调用方的就是它。)三站的次序也就一清二楚了:打头的是 _start,跟上的是 __libc_start_main,压轴的才是 main。在第二站拍的 backtrace 里,调用方也现身了:_start 调的它。另外的两个小细节,得跟您交代:__libc_start_main 的家在 libc 里,而 gdb 起跑的时候,进程里连 libc 的影子都没有,这个断点得用 set breakpoint pending on 挂成待定的,等 libc 进了场它才生根。b main 则落在了 main+9 上,跳过的是函数序言,这属于 gdb 的老习惯。

_start 自己长什么样?整段贴给您,gdb 的前导提示行笔者裁了,咱们挑要紧的看:

```text
Dump of assembler code for function _start:
=> 0x0000555555555070 <+0>:	f3 0f 1e fa        	endbr64
   0x0000555555555074 <+4>:	31 ed              	xor    %ebp,%ebp
   0x0000555555555076 <+6>:	49 89 d1           	mov    %rdx,%r9
   0x0000555555555079 <+9>:	5e                 	pop    %rsi
   0x000055555555507a <+10>:	48 89 e2           	mov    %rsp,%rdx
   0x000055555555507d <+13>:	48 83 e4 f0        	and    $0xfffffffffffffff0,%rsp
   0x0000555555555081 <+17>:	50                 	push   %rax
   0x0000555555555082 <+18>:	54                 	push   %rsp
   0x0000555555555083 <+19>:	45 31 c0           	xor    %r8d,%r8d
   0x0000555555555086 <+22>:	31 c9              	xor    %ecx,%ecx
   0x0000555555555088 <+24>:	48 8d 3d da 00 00 00	lea    0xda(%rip),%rdi        # 0x555555555169 <main>
   0x000055555555508f <+31>:	ff 15 2b 2f 00 00  	call   *0x2f2b(%rip)        # 0x555555557fc0
   0x0000555555555095 <+37>:	f4                 	hlt
End of assembler dump.
```

打头的两条是例行公事:endbr64 属于控制流防护的指令,另一条 xor 把帧指针的基址寄存器清了零,标记的是最外面的一帧,backtrace 走到这儿就知道该收了。中间的那几条,才是真正的内容:x86-64 的调用约定里,头六个参数走的,就是 rdi 到 r9 的六个寄存器,而 _start 正在给谁备参数?末尾的 call 已经招了,答的就是 __libc_start_main。咱们按寄存器逐个对:

- rdi 这边装的是 main 的地址,由 lea 那条指令完成。您瞧,main 在这儿就是一个被递来递去的函数指针。
- rsi 和 rdx 装的是 argc 与 argv:pop %rsi 弹出来的是 argc,mov %rsp,%rdx 取的是 argv。内核把命令行按约定摆在了栈顶,是留给进场的人取用的。
- r9 装的是第六个参数:mov %rdx,%r9 把进场时 rdx 里的原值挪了过去,那是动态链接器托付的收尾函数,回头要挂到退出钩子上的,咱们在讲 exit 的时候还会碰见它。

rcx 和 r8 的清零值得多说两句。翻过老教材的您,八成见过的是另一副长相:rcx 装的是 __libc_csu_init,r8 装的是 __libc_csu_fini,初始化与收尾的活,当时全由它们俩给包办了。咱们这套 glibc 2.44 上不是这样:2.34 起 glibc 把它们删了,main 之前那一串准备活的遍历,挪进了 libc 自己,于是链接器捎带的那几个文件干脆清零交差。教程旧了,对上的号也就乱了,这也是咱们这一篇全程拿实测说话的原因。

末尾的 call *0x2f2b(%rip) 也别放过。它调用 __libc_start_main 的方式,走的是 GOT 的间接一路,目标槽的位置在 0x555555557fc0。咱们拿 readelf -r 一查,这个槽的来历是 R_X86_64_GLOB_DAT,加载期就解析好的那一类。09 的 LD_DEBUG 时间线里咱们亲眼看过,__libc_start_main 的绑定排在 start 打印之前,懒绑定是轮不到它的。道理倒是直白:目标的真身住在 libc 里,链接期的时候没人知道它的落点,走表就成了仅有的写法。

## __libc_start_main:接过 main 的地址之后

__libc_start_main 接过 main 的地址,往后它干了什么?咱们想再往深一层,断一个叫 __libc_start_call_main 的函数,却没断成:本机 libc 的符号表里没有这个名字,咱们查的这几个名字里,nm -D 愿意交出来的只有 __libc_start_main 一个,局部符号被剥掉了。咱们退一步,改成了按地址断。把 __libc_start_main 的反汇编翻出来看,它身子里 call 的目标落在 0x7ffff7c27700,断的就是它:

```text
Breakpoint 2, 0x00007ffff7c27700 in ?? () from /usr/lib/libc.so.6
#0  0x00007ffff7c27700 in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff7c278b9 in __libc_start_main () from /usr/lib/libc.so.6
#2  0x0000555555555095 in _start ()
rdi            0x555555555169      93824992235881
rsi            0x1                 1
rdx            0x7fffffffd438      140737488344120
```

(到达之前那几站的重复输出,连同停点的反汇编行与其后的程序输出,笔者一并裁了。名字显示成了 ??,原因就是上面说过的符号被剥。0x7ffff7c27700 这个位置的函数,glibc 的源码里管它叫 __libc_start_call_main,2.34 起从 __libc_start_main 里拆出来的,咱们引用的是源码名。)寄存器里躺着的正是老三样:rdi 的值是 0x555555555169,跟 _start 里 lea 的目标一字不差,装的正是 main 的地址。rsi 是 argc 的 1,rdx 是 argv 的地址。咱们从这里再单步,就进 main 了。

__libc_start_main 自己的活,笔者的反汇编里看到的是:它的开头,把 r9 里那个收尾函数用 __cxa_atexit 登记了。动态链接器的初始化活计,咱们在前面讲两阶段的 trace 末尾已经见过一回,arch_prctl、set_tid_address、getrandom 的调用,说的就是它。接着把 .init_array 的构造表走了一遍,把 main 叫了起来,把返回值交给了 exit——构造表里住着谁,咱们下面专节一个个认。这几步没一样是咱们的代码,可每一样都排在 main 的前面。

## 启动文件那一排:_start 是谁放进来的

05 里咱们留过话:_start 在 main 前头干了什么,启动文件的那一排各管什么,这些话咱们今天来兑现。咱们倒过来问头一个问题:_start 又不是咱们写的,它从哪儿来?

答案就藏在链接的命令行里。咱们给 gcc 挂 -v 重链了一次,把 collect2 的那一行完整地抓了下来。collect2 是 01 里交代过的,链接器 ld 的包装:

```text
 /usr/lib/gcc/x86_64-pc-linux-gnu/16/collect2 ... -dynamic-linker /lib64/ld-linux-x86-64.so.2 -pie -o app_v /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/Scrt1.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/crti.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/crtbeginS.o -L. ... /tmp/ccXT99dK.o -lcalc -lgcc -lgcc_s_asneeded -latomic_asneeded -lc -lgcc -lgcc_s_asneeded /usr/lib/gcc/x86_64-pc-linux-gnu/16/crtendS.o /usr/lib/gcc/x86_64-pc-linux-gnu/16/../../../../lib/crtn.o
```

(省略号是笔者加的,裁掉的是 -plugin 一组、--build-id 一组和一串 -L 目录,顺序是原样保留的。)看排布就成了:打头的是 Scrt1.o,跟上的是 crti.o 和 crtbeginS.o,中间才是咱们的目标文件和 -lcalc 一排库,收尾换上的就是 crtendS.o 和 crtn.o。排在这一串里的五个文件,collect2 一个都没跟咱们商量,默默就捎上了,01 里咱们叫过它们 C 运行时的启动文件。顺带您还看到了 -dynamic-linker 的参数。它写的路径就是 ld-linux,落的正是程序头表里的 INTERP。

Scrt1.o 值得咱们细看。它的符号表,nm 亮出来的全在这儿:

```text
$ nm Scrt1.o | grep -v ' [a-z] '
                 U _GLOBAL_OFFSET_TABLE_
0000000000000000 R _IO_stdin_used
0000000000000000 D __data_start
                 U __libc_start_main
0000000000000000 T _start
0000000000000 W data_start
                 U main
```

咱们当场就认出了两位老熟人。一位是 U 的 main,另一位是 U 的 __libc_start_main。05 里咱们链 no_main 失败的那回,报错里的 undefined reference to `main`,引用它的正是咱们眼前的这个文件。它的 .text,就是咱们上面看过的十三条指令,差别的部分全落在重定位上:

```text
$ readelf -rW Scrt1.o | head -4
Relocation section '.rela.text' at offset 0x2c8 contains 2 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
000000000000001b  000000040000002a R_X86_64_REX_GOTPCRELX 0000000000000000 main - 4
0000000000000021  0000000800000029 R_X86_64_GOTPCRELX     0000000000000000 __libc_start_main - 4
```

在 .o 的版本里,main 与 __libc_start_main 的取址,走的都是过 GOT 的写法。等到链接成了 09 里认过身份的 PIE,链接器把过 GOT 取 main 的写法,放松成了 lea 直取,咱们在 gdb 里看到的 lea 就是它的成果,而 __libc_start_main 住在 libc 里,倒是放松不动,保持着走表的写法。重定位名字里挂的 GOTPCRELX 后缀,就是给放松留的口子,咱们不展开。

那一排里剩下的四位,咱们一人一句话交代:

- crti.o 里住着 _init 和 _fini 两个函数的开头。05 里咱们见过的 __gmon_start__ 弱符号,它的钩子正身就在 crti.o 的 _init 里。链接完成后的样子,咱们看真的:

```text
0000000000001000 <_init>:
    1000:	endbr64
    1004:	sub    rsp,0x8
    1008:	mov    rax,QWORD PTR [rip+0x2fc9]        # 3fd8 <__gmon_start__>
    100f:	test   rax,rax
    1012:	je     1016 <_init+0x16>
    1014:	call   rax
    1016:	add    rsp,0x8
    101a:	ret
```

您品这个结构:开头的 endbr64 和 sub 来自 crti.o,结尾的 add 和 ret 来自 crtn.o,_init 的身子是链接器拿两头的碎片拼出来的。夹在中间的四条,可就不属于两头了,它们是 __gmon_start__ 的钩子:有定义的时候就会调用,查到的值是零,就直接跳了过去。咱们的程序里,它是 w 的未定义弱符号,所以这几条在这里是空转的。等性能剖析的工具链接上了,它就有了用武之地。05 里咱们讲过弱符号留口子的用意,这里看到的是它在启动流程里的落点。

- crtbeginS.o 贡献的是 .init_array 里的第一项 frame_dummy,和 .fini_array 里的第一项 __do_global_dtors_aux,外加的两个小函数 deregister_tm_clones、register_tm_clones。TM 是事务内存(transactional memory)的缩写,这俩同样是弱引用挂的钩子,没人定义的时候照样跳过。
- crtendS.o 拿出的是两个边界符号,__FRAME_END__ 和 __TMC_END__,给 crtbeginS 那头数表的时候当终点用。
- crtn.o 在 nm 的输出里一条符号都没有。它是 .init 和 .fini 的尾巴,上面 _init 的末两条就是它的全部内容。

您再留意文件名里的 S 后缀:Scrt1 的 S、crtbeginS 的 S,标记的就是 PIE 版本。非 PIE 的链接,上场的就是 crt1.o 和 crtbegin.o,干的活是同款。

## .init_array:main 之前的那张构造表

启动文件把 _start 送进了场,libc 接下来要走的表,咱们接着看。C++ 全局对象的构造函数,登记的地方就是 .init_array。咱们写一个带构造的全局对象:

```cpp
// guard.cpp —— 一个带构造函数的全局对象
#include <cstdio>

struct Guard {
    Guard() { std::printf("Guard ctor: main 还没进门\n"); }
    ~Guard() { std::printf("Guard dtor: main 已经退场\n"); }
};

Guard g_guard;

int main() {
    std::printf("main: 这才轮到我\n");
    return 0;
}
```

```text
$ g++ guard.cpp -o guard && ./guard
Guard ctor: main 还没进门
main: 这才轮到我
Guard dtor: main 已经退场
```

构造跑在了 main 的前面,析构跑在了 main 的后面。表住在哪儿?readelf 替咱们交了底:

```text
$ readelf -SW guard | grep -E 'init_array|fini_array'
  [21] .init_array       INIT_ARRAY      0000000000003d98 002d98 000010 08  WA  0   0  8
  [22] .fini_array       FINI_ARRAY      0000000000003da8 002da8 000008 08  WA  0   0  8
$ objdump -s -j .init_array guard
Contents of section .init_array:
 3d98 40110000 00000000 99110000 00000000  @...............
$ nm guard | grep -E 'frame_dummy|_GLOBAL__sub_I'
0000000000001199 t _GLOBAL__sub_I_g_guard
0000000000003d98 d __frame_dummy_init_array_entry
0000000000001140 t frame_dummy
```

十六个字节的 .init_array,咱们按八字一项地数,正好数出了两项。0x1140 的 frame_dummy,是 crtbeginS.o 贡献的。nm 多贴出来的那一行 __frame_dummy_init_array_entry,住的正是表里第一项的位置,链接的时候安放的,frame_dummy 的来路,这一下也就对上了。0x1199 的 _GLOBAL__sub_I_g_guard,则是编译器替 g_guard 生成的构造包装,名字长成了这样,咱们的源码里找不到它,它是编译器造的。libc 启动的时候,挨个地把表里的项调了一遍,调完了才进的 main。__attribute__((constructor)) 标记的函数,塞的也是同一张表。

两项的次序也有讲究:排在前面的是 frame_dummy,咱们的那项跟在后面,跟链接行的排布一致,crtbeginS.o 排在咱们的目标文件前面。跨翻译单元的构造顺序,标准是没有规定的,构造次序没担保的风险、函数局部 static 的对策,12 里专门开了一节来讲。咱们这边只认载体:C++ 全局对象的构造时机,机制上说的就是表被走的时机。

## 静态对拍:少一层,少一整段

动态装载的这一层到底添了多少活,咱们不空口估,把同一份标本全静态地链一遍来对拍。libcalc 也顺手打成了 .a:

```text
$ gcc -c libcalc.c -o libcalc.o && ar rcs libcalc.a libcalc.o
$ gcc main.c -I. -L. -lcalc -static -o app_static
$ file app_static
app_static: ELF 64-bit LSB executable, x86-64, version 1 (GNU/Linux), statically linked, BuildID[sha1]=5e7babfe8014dfd92d593711a602627c28b22612, for GNU/Linux 4.4.0, not stripped
$ readelf -h app_static | grep -E "Type|Entry"
  Type:                              EXEC (Executable file)
  Entry point address:               0x402ea0
$ readelf -l app_static | grep -E "INTERP|Requesting"
$ echo $?
1
```

(末尾的 grep 找了个空,退出码回到了 1:INTERP 在程序头表里的类型名 PT_INTERP,在这个文件里已经不存在了,笔者特意用退出码把它点了出来。)Type 变成了 EXEC:04 里欠的那半句话,固定地址的可执行文件则是 EXEC,到这儿兑现了,Entry 的 0x402ea0 也是固定基址段里的地址。更痛快的是 strace 的对照:

```text
$ strace -o static.strace ./app_static && wc -l static.strace e1.strace
19 static.strace
51 e1.strace
70 total
$ sed -n '1,4p' static.strace
execve("./app_static", ["./app_static"], 0x7fff89cad5c0 /* 69 vars */) = 0
brk(NULL)                               = 0x219f4000
brk(0x219f4d40)                         = 0x219f4d40
arch_prctl(ARCH_SET_FS, 0x219f43c0)     = 0
```

等 execve 落了地,直接就是 libc 的自身初始化:brk、arch_prctl 的那几条,libc 的代码已经整个烙在文件里了,不用装了。中间的 openat、mmap 一个都没有:ld.so.cache 没影了,库和动态链接器也没影了。19 行对 51 行的对照(咱们数的是写进文件的行数,往终端跑会多一条 ioctl 的查询),少掉的那 32 行,少的就是第二阶段的整段。08 里咱们看过共享库给内存省了什么,这一篇补上的是代价的一面,所以动态装载这一层,是追加在启动路上的一整段流程。

## main 之后,半眼

全卷咱们都停在 main,今天既然把启动看全了,顺势往 main 的后面瞟上半眼也就够了。guard 的输出早就替咱们看过了:析构落在了 main 的后面。机制上也只是一句话的事:main 的返回值交给 exit,exit 依着 __cxa_atexit 的登记倒着收尾,r9 里交代的那个收尾函数也在册,接下来走的是 .fini_array,最后的 exit_group 系统调用落幕,trace 里压轴的系统调用就是它。

## CMake 视角

启动的题目下,CMake 那边常被问起的开关,咱们这篇只看 pie 一个。咱们摆两个目标对拍,库咱们打成了 STATIC,省得再摆一套动态的:

```cmake
add_library(calc STATIC libcalc.c)
add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
add_executable(app_fixed main.c)
target_link_libraries(app_fixed PRIVATE calc)
target_link_options(app_fixed PRIVATE -no-pie)
```

(calc.h 的 include 目录一行笔者裁了,完整的在实验包里)

```text
$ cmake -S . -B build && cmake --build build
$ readelf -h build/app | grep -E "Type|Entry"
  Type:                              DYN (Position-Independent Executable file)
  Entry point address:               0x1050
$ readelf -h build/app_fixed | grep -E "Type|Entry"
  Type:                              EXEC (Executable file)
  Entry point address:               0x401050
```

两个目标跑了起来,输出的内容一字不差。CMake 交给 GCC 的默认就是 pie,Type 记的是 DYN。挂上 -no-pie 就换回了 EXEC,Entry 也跟着换了,从相对地址换成了 0x401050,回到了 0x40 打头的传统固定基址段。有个容易误会的点,咱们再补一句:app_fixed 的身上依然带着一条 INTERP,它照样动态地链着 libc。-no-pie 动的是地址的写法,链接的方式它一概不碰,您想要静态,那是 -static 的活,两个开关各管各的。运行期找库的那点事,构建侧的入口是 BUILD_RPATH,10 的 CMake 节讲过,咱们不重演。

## 接下来:链接期还能再挤出什么

咱们走到了这儿,从 execve 到 main 的整条路,咱们算是走完了:内核装的是头两个,动态链接器拉起了整棵的依赖树,_start 把 main 递了出去,__libc_start_main 走完了 .init_array,咱们写的第一行代码,到这儿才算是跑了起来。咱们回头数数,这一路用掉的系统调用不过几十条,可每一步都是有名有姓的。

故事到这儿讲完了,咱们留个念想。咱们这一卷读到这儿,编译器的优化始终只看得见单个的翻译单元,链接器倒是一次拿齐了所有的目标文件,可它干的,只是搬位置、接符号的活儿,除了重定位的那点改写,优化的事它一件不干。咱们要是把优化挪到链接期,让它拿着每个翻译单元的完整信息干活,能挤出什么?链接时优化(LTO)说的就是它,这正是 15(LTO 与现代链接器)的正题,咱们下一篇见。
