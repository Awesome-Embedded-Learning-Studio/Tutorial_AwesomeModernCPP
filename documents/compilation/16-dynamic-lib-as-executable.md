---
chapter: 13
difficulty: intermediate
order: 16
platform: host
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 链接器
- 工具链
title: "番外:动态库能当可执行文件跑吗"
description: "默认 .so 直接跑是段错误:strace 里 execve 成功、SIGSEGV 紧随,gdb 实证 RIP 落在只读的文件头那页,Entry 0x0 加基址就是落点;反例 libc.so.6 直接跑出十行 banner,入口 __libc_main 一条 write 加 _exit,还带着 INTERP 走标准两阶段;再用 -Wl,-e 指入口、裸系统调用打 banner,做出 libdual.so 一份二进制三种用法——直接跑、被链接、被 dlopen,路上还撞上并解决了栈保护 canary 在裸入口的崩溃;ld.so 自己也是带入口的 DYN;CMake 的 CXX_VISIBILITY_PRESET 与 target_link_options 把构建落进工程,POST_BUILD 直接跑产物当冒烟。"
cpp_standard: [11, 14, 17, 20]
---

# 番外:动态库能当可执行文件跑吗

上一篇收正文的时候留过一句话:libmyutil.so 头部登记的 Entry 是 0x0,它到底能不能直接跑、硬跑了会发生什么,番外整篇玩的就是这个。今天咱们来交卷。答案咱们摆在前头,一共三层:默认做出来的 .so 跑不动,它崩在了入口。libc.so.6 是能跑的,因为人家的入口是真的。咱们自己也能搓一个能跑的,给链接器指一个入口就是了。三层的证据全部本机真跑,咱们一层一层来。

环境咱们照例交代:笔者的实验都在 WSL2 里跑,工具链用的是 GCC 16.2.1 加 binutils 2.47,glibc 到了 2.44,CMake 到了 4.4.3,strace 拿的是 7.2,gdb 拿的是 17.2,14 里报过的六个版本,今天在咱们机器上一个都没换。这一篇里的每一段输出,都是真实跑出来的。

## 亲手撞一把:三种文件,三种下场

标本还用卷里的老面孔,08 里做动态库用的命令咱们原样再来一遍,b.c 还是 05 的两个老定义:

```text
$ gcc -shared -fPIC -o libmyutil.so b.c
$ ls -l libmyutil.so
-rwxr-xr-x 1 charliechen charliechen 15176 Oct  2 16:40 libmyutil.so
```

有一处得跟您如实交代。照多数材料的说法,.so 出厂是不带执行位的,想跑的话就得补一回 chmod。笔者的机器上偏偏不是这么回事:本机的 ld 给产物落的直接就是 755,执行位是天生带着的。权限这一关咱们想亲眼看看,只好自己动手把它摘了:

```text
$ chmod -x libmyutil.so
$ bash -c './libmyutil.so; echo $?'
bash: line 1: ./libmyutil.so: Permission denied
126
```

这一回 execve 是真到了内核手上的:内核一看没有执行位就把它顶了回去,退的错误码是 EACCES(权限不足),bash 把这个码翻译成了 Permission denied,交回的退出码是 126——找着了文件、却没能真执行,shell 给的就是这个老约定码。咱们把执行位还回去再跑:

```text
$ chmod +x libmyutil.so
$ bash -c './libmyutil.so; echo $?'
bash: line 1: 206363 Segmentation fault         ./libmyutil.so
139
```

这回内核把文件收下了,进程也起来了,起来下一瞬的收场是段错误:退出码 139 的来历是 128 加 11,内核报 SIGSEGV 的老编码,13 里咱们对过一笔。一个下场咱们看着还不够立体,咱们把 05 的 a.o 与 08 的 dyn_app 也请出来。a.o 是 gcc -c 编出的目标文件,dyn_app 是拿 -lmyutil 链出来的老程序,a.o 也补上了执行位,免得权限抢了戏,咱们把三种文件摆一排:

```text
$ bash -c './a.o; echo $?'
bash: line 1: ./a.o: cannot execute binary file: Exec format error
126
$ bash -c './libmyutil.so; echo $?'
bash: line 1: 206378 Segmentation fault         ./libmyutil.so
139
$ LD_LIBRARY_PATH=. ./dyn_app; echo $?
42
```

三种文件的下场一个比一个走得远:.o 连内核的检查都没过,.so 过了内核、起了进程,死在了半路。正常程序走完了全程,拿 42 交了卷。咱们把三位的头部并排一读,差异就现了形:

```text
$ readelf -h a.o | grep -E "Type|Entry"
  Type:                              REL (Relocatable file)
  Entry point address:               0x0
$ readelf -h libmyutil.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x0
$ readelf -h dyn_app | grep -E "Type|Entry"
  Type:                              DYN (Position-Independent Executable file)
  Entry point address:               0x1040
```

(readelf -h 的完整输出有二十行,笔者各留了 Type 与 Entry 两行。)a.o 的 Type 是 REL:内核的 ELF 装载器只受理 EXEC 与 DYN 两种类型,REL 不在受理的名单里,execve 当场就退了回去,退回的错误码是 ENOEXEC。报错文本里的 Exec format error 就是它的长相,内核的意思很直白:这个 ELF 的类型它不认识。不过 bash 拿到这个码还会抢救一把,把它当 shell 脚本再试了一次,结果一看是二进制,这才报了 cannot execute binary file。libmyutil.so 与 dyn_app 的 Type 倒是一样的。04 里咱们对过,pie 程序写的也是 DYN,括号里的区别只是 readelf 的注脚:它按动态段里 DF_1_PIE 的标记,把 PIE 认了出来,Type 的本尊没有分过家。真正分家的还是 Entry:程序是有入口的,.so 的入口是 0。那 Entry 0x0 的 DYN,内核凭什么放行了它、进程又死在了哪一步,咱们抓现行。

## 为什么崩:execve 成功了,死在交权之后

咱们把 strace 请出来。14 里它陪着咱们走完了整个启动链,今天咱们只看头尾:

```text
$ strace -o /tmp/w3.txt ./libmyutil.so
$ cat /tmp/w3.txt
execve("./libmyutil.so", ["./libmyutil.so"], 0x7fffba6301c0 /* 70 vars */) = 0
--- SIGSEGV {si_signo=SIGSEGV, si_code=SEGV_ACCERR, si_addr=0x7c7a94ad4000} ---
+++ killed by SIGSEGV +++
```

(咱们写进文件再看,trace 的全文就这三行,咱们一行都没裁。si_addr 与栈上的 env 指针是这一次跑出来的值,复跑会换新的,死讯却是不会换的。)头一行的内容就够意外:execve 返回了 0,成了。14 里 execve 后头跟着的那一长串 openat、mmap,咱们一个都找不到:此刻装库的活还没轮到谁干,不过它眼下也用不着,一会儿咱们就懂。紧跟着的第二行就是死讯,还捎带了三个字段:si_signo 报的是 SIGSEGV,si_code 给的是 SEGV_ACCERR,意思说的是地址有映射、权限不对,地址压根没映射时给的会是 SEGV_MAPERR。si_addr 记的是 0x7c7a94ad4000,它是页对齐的,长得像一个映射的基址。内核在 execve 里干的事,咱们对一遍:内核按程序头的 LOAD 段把文件映射进内存,DYN 类型拿的是 pie 同款待遇,给的是一个随机基址。眼前这个文件是没有 INTERP 段的,第二个文件也就不用装了。然后内核把控制权交给头部登记的入口。入口是 0x0?其实内核不检查这个,基址加 0 也照样跳了过去。跳到哪儿了,gdb 替咱们拍了下来(它起进程时地址随机是关着的,数字是稳的):

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex run -ex 'p/x $rip' -ex 'info proc mappings' ./libmyutil.so
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7ffa000 in ?? ()
$1 = 0x7ffff7ffa000
0x00007ffff7ffa000 0x00007ffff7ffb000 0x1000  0x0     r--p  /tmp/.../lab/libmyutil.so
0x00007ffff7ffb000 0x00007ffff7ffc000 0x1000  0x1000  r-xp  /tmp/.../lab/libmyutil.so
```

(gdb 起跑的提示几行、process 行与 maps 的表头,vvar、vdso 与 stack 的几段,libmyutil.so 的后两条 LOAD,笔者裁了,长路径裁短了,列间的空白也压紧了,地址是原样的。)RIP 的值是 0x7ffff7ffa000,maps 的头一行恰好就是它:libmyutil.so 的第一条 LOAD,文件偏移记的是 0x0,权限给的也是 r--p。真相到这儿就齐了:所谓入口 0x0 加上基址,落点就是文件的第一页,ELF 头住的那一页,它是只读的,没有执行的权限。CPU 到了那儿取指令,MMU 报的正是权限不对,与 strace 里的 SEGV_ACCERR 对上了。所以默认的 .so 没有吃到拒绝,内核把它照单全收地执行了:execve 成了功,控制权也交了出去,然后进程死在了交接的落点上。08 里咱们写下过半句:Entry 写的是 0x0,等于明说了没有入口。下半句笔者今天补全给您看了:它死就死在文件头的那一页上。

## 反例:libc.so.6 就能跑

默认的 .so 跑不动,事情到这儿就定了?还真有跑得动的,而且就是您天天在用的这个。glibc 的本尊,咱们直接敲下去:

```text
$ /usr/lib/libc.so.6; echo $?
GNU C Library (GNU libc) stable release version 2.44.
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC version 16.2.1 20260810.
libc ABIs: UNIQUE IFUNC ABSOLUTE
Minimum supported kernel: 4.4.0
For bug reporting instructions, please see:
<https://gitlab.archlinux.org/archlinux/packaging/packages/glibc/-/issues>.
0
```

banner 打了十行,进程体面地退了场,交回的退出码是 0。同一个 DYN 的身份,它凭什么行?咱们把它的家底翻出来,跟前一位的家底对着比(执行位的那一关它也是天生过关的:ls -l 看到的也是 755):

```text
$ readelf -h /usr/lib/libc.so.6 | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x279c0
$ readelf -l /usr/lib/libc.so.6 | grep -A2 INTERP
  INTERP         0x00000000001c4be0 0x00000000001c4be0 0x00000000001c4be0
                 0x000000000000001e 0x000000000000001e  R      0x10
      [Requesting program interpreter: /usr/lib/ld-linux-x86-64.so.2]
```

摆出来的家底足足有三样。执行位是天生带着的。Entry 登记的是 0x279c0,一个不折不扣的非 0 值。程序头表里还多出来了一条 INTERP,登记的 interpreter 是 ld-linux。INTERP 登记的路径还有个小差异:14 的链接行里,GCC 递的默认是 `/lib64/ld-linux-x86-64.so.2`,本机 libc.so.6 的 INTERP 写的却是 `/usr/lib/ld-linux-x86-64.so.2`——路径是构建时定死的,不过 /lib 与 /lib64 在本机都是软链,两条落到的是同一个文件。头两样是跑起来的硬条件,第三样的意思是:它跑的时候走的是 14 讲过的标准两阶段,内核把 libc.so.6 本尊连同它的 interpreter 一并装好,动态链接器做完了重定位,再把控制权交到 0x279c0 的手上。住在这个入口里的又是谁?咱们把入口翻出来看:

```text
$ objdump -d --start-address=0x279c0 --stop-address=0x27a20 /usr/lib/libc.so.6
00000000000279c0 <__libc_main>:
   279c0:	f3 0f 1e fa          	endbr64
   279c4:	55                   	push   %rbp
   279c5:	bf 01 00 00 00       	mov    $0x1,%edi
   279ca:	ba e3 01 00 00       	mov    $0x1e3,%edx
   279cf:	48 8d 35 ea aa 19 00 	lea    0x19aaea(%rip),%rsi        # 1c24c0 <banner>
   279d6:	48 89 e5             	mov    %rsp,%rbp
   279d9:	e8 62 ac 0e 00       	call   112640 <__write>
   279de:	31 ff                	xor    %edi,%edi
   279e0:	e8 8b e6 0b 00       	call   e6070 <_exit>
```

(objdump 的报头几行、末尾对齐的空指令,连同停址以里捎带的下一个函数 _Unwind_Resume,笔者裁了。)入口的名字叫 __libc_main,身子是一条直线:write 的三个参数备齐,edi 装的是 1 号描述符,rsi 指着的是 banner 串,edx 拿的是 0x1e3。call 的是 __write,glibc 的内部别名,libc 的自己人直呼,是不走 PLT 的。write 干完了活,edi 清了零,末了拿 call _exit 收的场。0x1e3 的十进制是 483,咱们拿 wc -c 量过,上面十行 banner 的字节数一个不差。您留意它没用 printf:入口拿到控制权的时候,__libc_start_main 其实还没跑过,stdio 的缓冲一个都没立起来,所以 glibc 自己也不赌这个,用的就是最原始的 write 加直接退出。这样的谨慎,咱们接下来正好用得上。

## 手搓一个:同一个文件,三种用法

看会了 libc.so.6 的路数,咱们自己搓一个。咱们把目标定得齐整,一个 .so 要伺候的三种用法是:直接跑、被别人链接、被 dlopen,交出去的是同一份二进制,而且连一个字节都不用改。咱们把十七行的源码全贴出来:

```cpp
// dual.cpp —— 同一个文件,打算伺候三种用法:直接跑、被链接、被 dlopen
// 对外的接口:extern "C" 裸名 + 默认可见,链接与按名取符号的用户都认它
extern "C" __attribute__((visibility("default"))) int add(int a, int b) {
    return a + b;
}

// 进程入口:直接跑的时候,控制权落在这儿。标 hidden,不出导出表
extern "C" __attribute__((visibility("hidden"))) void dual_start() {
    const char msg[] = "dual: 作为可执行文件跑起来了\n";
    // write(1, msg, len):syscall 号 1。不碰 stdio:入口的栈是裸的,
    // __libc_start_main 没跑过,咱们只做两条系统调用,别的什么都不赌
    asm volatile("syscall" : : "a"(1L), "D"(1L), "S"(msg), "d"(sizeof(msg) - 1) : "rcx", "r11", "memory");
    // exit_group(0):syscall 号 231。不用 60(exit):60 只收当前线程,
    // 231 才收整个进程,正经收场走的都是它
    asm volatile("syscall" : : "a"(231L), "D"(0L) : "rcx", "r11", "memory");
    __builtin_unreachable();  // exit_group 一去不回
}
```

两位咱们各交代两句。add 是对外的接口:`extern "C"` 把名字保成了裸的,13 那回忘了它,dlsym 拿着 calc_plugin_entry 的名字去找,表里躺的却是 _Z17calc_plugin_entryv,扑了个空。default 可见性是给它留的口子,配上构建期的全局收窄,正是 11 立过的那对搭档。dual_start 是给直接跑准备的入口,标了 hidden,是不出导出表的。它的身子里只有两条系统调用:write 把 banner 写到 1 号描述符,再拿 exit_group 收的场。14 的 trace 里,压轴的就是它,单线程的进程拿 60 号的 exit 也能退,不过 60 只收当前线程,收整个进程的是 231,咱们按正经的写。为什么抠得这么严?咱们这个 .so 不打算带 INTERP,内核装完了 LOAD 段就直接交权,重定位的活没人做,GOT 的表没人填,运行库是一个字节都没初始化的,入口的代码必须自给自足:syscall 的指令本身不依赖重定位,banner 的字节让编译器用 movabs 的 64 位立即数直接搬进了栈、取址走的是 rbp 相对寻址,同样一条重定位都省了,咱们靠 write 加 exit_group 两条调用就够活了。构建的命令如下:

```text
$ g++ -fno-stack-protector -shared -fPIC -o libdual.so dual.cpp -Wl,-e,dual_start
$ readelf -h libdual.so | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x10fd
```

`-Wl,-e,dual_start` 的意思是往链接器递一个 `-e`(entry)的开关:登记入口用的就是它。`-Wl` 拿逗号递参数的写法,08 的 -soname、10 的 -rpath 用的都是它。Entry 从 0x0 变成了 0x10fd,dual_start 的地址。至于 -fno-stack-protector 的来历,说来是笔者真翻过的车:头一版的命令没带它,直接跑出来的还是 139。咱们拿 gdb 一看:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex run -ex 'x/2i $pc' ./libdual_sp.so
Program received signal SIGSEGV, Segmentation fault.
0x00007ffff7ffb101 in dual_start ()
=> 0x7ffff7ffb101 <dual_start+4>:	mov    %fs:0x28,%rax
   0x7ffff7ffb10a <dual_start+13>:	mov    %rax,-0x8(%rbp)
```

(gdb 起跑的提示几行与空行,笔者裁了。)死点落在了 dual_start+4,咱们的代码里可没有这样的 mov。它去读的是 fs:0x28,那是栈保护的序言:编译器默认给函数插的 canary(金丝雀,一道防栈溢出改写的校验值)就存在这儿,取值的路得靠 FS 段基址走进线程局部存储。进程的入口这儿 TLS 还没建立,FS 的值是 0,读地址 0x28 当场就死了。咱们的源码明明一行标准库都没碰,默认的加固选项替咱们碰了。补上了 -fno-stack-protector,序言撤了,程序这才算跑通了。该验收了,咱们把三种用法挨个跑一遍:

```text
$ ./libdual.so; echo $?
dual: 作为可执行文件跑起来了
0
$ g++ -std=c++17 host.cpp -L. -ldual -o host && LD_LIBRARY_PATH=. ./host; echo $?
host: add(1,2)=3
0
$ g++ -std=c++17 dlhost.cpp -o dlhost && ./dlhost; echo $?
dlhost: add(1,2)=3
0
```

(host.cpp 的活是链它调 add,dlhost.cpp 按 13 的三步走 dlopen 加 dlsym,两份都是几行的小程序,笔者收在实验包里了,不占正文的行数。)头一种用法的全程没有动态链接器什么事:内核装好了段,把控制权交到 dual_start 的手上,两条系统调用跑完就退了场。后两种用法里 add 的待遇没变:照常被解析、被调用,一个走 -l 的登记老路,一个走运行期装载的老路。导出表咱们也验一眼:

```text
$ nm -D libdual.so
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
00000000000010e9 T add
```

(w 的弱钩子是链接器的随件,四条里笔者裁了两条,剩下的也出不了门。)门口剩下的就是干干净净的裸名 add,13 的裸名写法这回生下来就守住了。一个文件把三种活法全占了,到这儿就算齐了。

## 动态链接器自己也是程序

末了看一位身份特殊的。14 里 ld-linux 的岗位是 interpreter,站在别人的程序头里干活,今天咱们把它自己请到 execve 的对面:

```text
$ /lib64/ld-linux-x86-64.so.2; echo $?
/lib64/ld-linux-x86-64.so.2: missing program name
Try '/lib64/ld-linux-x86-64.so.2 --help' for more information.
1
$ readelf -h /lib64/ld-linux-x86-64.so.2 | grep -E "Type|Entry"
  Type:                              DYN (Shared object file)
  Entry point address:               0x217e0
```

它其实没有崩,抱怨的是咱们少给了参数:它期待的用法是自己打头,后面跟的是一个可执行文件加参数,拿它显式地启动程序。--help 的开头就是它的自述:抬头写的正是 program interpreter,说的就是给动态链接程序当 interpreter 的身份,平时它是被自动请起来的,今天被咱们手动请了一回。Type 写的是 DYN,Entry 也是非 0 的,执行位也是带着的:平时替别人做装载的它,自己也是一个带入口的 DYN。同一个 ELF 的两种活法,今天两头咱们都亲眼见过了。

## CMake 视角

咱们把这套构建交给 CMake,样板的长度您一眼就能看完:

```cmake
cmake_minimum_required(VERSION 3.16)
project(dual_run CXX)
add_library(dual SHARED dual.cpp)
# 可见性:全局收窄,接口单点放开(dual.cpp 里 add 挂着 default 属性)
set_target_properties(dual PROPERTIES CXX_VISIBILITY_PRESET hidden)
# 入口的栈是裸的,禁掉栈保护的 canary 序言
target_compile_options(dual PRIVATE -fno-stack-protector)
# -Wl,-e,dual_start:把进程入口指到咱们自己的 dual_start
target_link_options(dual PRIVATE "-Wl,-e,dual_start")
# 执行位兜底(本机 ld 默认就给),顺手直接跑一把当冒烟
add_custom_command(TARGET dual POST_BUILD
    COMMAND /usr/bin/chmod +x $<TARGET_FILE:dual>
    COMMAND $<TARGET_FILE:dual>)
```

CXX_VISIBILITY_PRESET 是 11 的 CMake 节讲过的 C_VISIBILITY_PRESET 的 C++ 同款,咱们回指一句就走。入口与栈保护的两个开关,咱们在前面刚翻过车,进了工程就照着配上。咱们用 --verbose 构建一遍,链接行与冒烟都现了形:

```text
$ cmake -B build -S . && cmake --build build --verbose
[ 50%] Building CXX object CMakeFiles/dual.dir/dual.cpp.o
/usr/sbin/c++ -Ddual_EXPORTS  -fPIC -fvisibility=hidden -fno-stack-protector ... -c .../dual.cpp
[100%] Linking CXX shared library libdual.so
/usr/sbin/c++ -fPIC -shared -Wl,-e,dual_start -Wl,--dependency-file=... -Wl,-soname,libdual.so -o libdual.so CMakeFiles/dual.dir/dual.cpp.o
/usr/bin/chmod +x .../build/libdual.so
./libdual.so
dual: 作为可执行文件跑起来了
```

(CMake 与 make 的脚手架行,进出目录的提示、依赖检查、进度条都在其中,与末尾 Built target 的一行,笔者裁了,长路径与个别中段裁短了。)`-fvisibility=hidden` 上了每个编译行,add 的 default 属性单点放行,11 立过的搭档在工程里就是这个样子。`-Wl,-e,dual_start` 落在了链接行上,咱们要的就是这一眼。POST_BUILD 里的 chmod 一行,笔者照实交代:本机 ld 的产物本来就带执行位,它在咱们的机器上是空转的,留着是给执行位默认不给的构建环境兜底。后一条把产物直接跑了一遍,banner 进了构建日志,冒烟就算过了:能不能当程序跑的这一道验收,构建系统自己就替咱们办了。

## 卷终:边界在入口,不在格式

01 开卷立路线图的时候,番外名下挂的就是这道题:动态库,能不能当成可执行文件直接跑?今天可以落纸了。格式上其实没有分界:库与程序同为 ELF,DYN 的一栏两边都写得进,pie 程序写的也是 DYN,今天的 libdual.so 也是 DYN。分家的只有入口与用法:入口指着 _start、等着被 exec 的是程序,Entry 空着 0x0、等着被映射的是库,而且这样的边界还是可写的:咱们给链接器递一个入口的开关,库就有了自己的入口。咱们判断能不能跑的依据从来不是后缀,看的是入口的事有没有人办:glibc 办了,所以 libc.so.6 跑得体面。咱们自己也办了,所以 libdual.so 跑得起来。libmyutil.so 是没人办的,头部 Entry 停的就是 0x0,硬跑的下场是死在文件头上。正文十五篇讲的是机制,今天这一篇走到了机制的边界上,边界咱们亲眼见过,机制反而更清楚了。这一卷到这儿就收了,咱们别的卷里见。
