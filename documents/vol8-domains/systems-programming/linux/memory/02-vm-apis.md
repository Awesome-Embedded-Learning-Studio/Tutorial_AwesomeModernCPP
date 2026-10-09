---
title: "虚拟内存 API 全景:mprotect/madvise/mlock"
description: "拿到一段虚拟内存之后怎么管它:mprotect 管权限、madvise 管建议、mlock 管驻留。本篇实测 si_addr 逐字节跟随出错的那个字节(纠正 L02 的页首粗读法)、W^X 三步曲里 Linux 对 RWX 的放行与 macOS 的拒绝、text 加 W 走 COW 而 [vvar] 回 EACCES/[vdso] 回 EINVAL 的动不得清单、降权 .data 页后首次冷调用死于 ld.so 懒解析(handler 得把页权恢复才能开口)、MADV_RANDOM 关预读恰好 1024 KiB 而 NORMAL 在 3088 与 8192 KiB 之间自适应抖动、DONTNEED 匿名页读回零且 Rss 减半、DONTFORK 让子进程整段消失、MADV_REMOVE 在 ext4 上真打洞、mlock 顺手预故障与 RLIMIT_MEMLOCK 的 ENOMEM 边界;招牌实战 guarded_buffer<T> 用尾对齐 guard 页把无声越界变成 si_addr 直报越界第几字节,mincore 的三种驻留口径与 process_vm_readv 跨进程读一眼,文末预告 Windows PAGE_GUARD 的一次性陷阱"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 28
prerequisites:
  - "进程内存布局:/proc/pid/maps 全图"
  - "mmap 内存映射:把文件贴进地址空间"
related:
  - "进程内存布局:/proc/pid/maps 全图"
  - "共享内存:shm_open 与映射"
  - "虚拟内存:VirtualAlloc 与 VirtualProtect"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 内存管理
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 虚拟内存 API 全景:mprotect/madvise/mlock

[上一篇](./01-memory-layout.md)咱们把进程地址空间的全图画完了:maps 里那几十段的身份,咱们都挨个认过了门,哪段是可读的?哪段是可执行的?heap 从哪里长出来,栈顶又落在哪儿?图画好了,新的问题自然就冒出来了。mmap 把一段页贴进地址空间的时候,prot 参数给过它一份初始的权限,可权限并不是一成不变的,拿到了一段虚拟内存之后,咱们该怎么继续管它?

Linux 把这件事交给了三个 API,而它们各管一层。`mprotect` 管的是权限,咱们让一段原本可写的地址变得不可写,而一页只读的数据也能变得可执行,改的就是页表里每一页的保护位。`madvise` 管的是建议,您打算怎么用这段内存?是顺序扫、还是随机跳?近期还要不要它?咱们递给内核一句话,内核听不听、怎么听,咱们拿数字说话。`mlock` 管的是驻留,咱们把这些页留在物理内存里,而内核不许把它们换出去,实时音频和密钥页最在乎的正是这一层。这一篇咱们把三层各走一遍,最后把它们组装成一件实战的工具 `guarded_buffer<T>`,一个越界第几字节都当场报出来的缓冲区,收尾的再带上 mincore 与 process_vm_readv,两件观察的小工具。

[L02](../file-io/02-mmap-memory-mapping.md) 的 mprotect 一节咱们已经打过底:效果按的是整页生效,addr 咱们得自己对齐到页边界,越权的访问会送来 SIGSEGV,而 si_code 会分成 `SEGV_ACCERR`(映射在,权限不许)与 `SEGV_MAPERR`(地址根本没映射)两类。咱们接着深讲的,全是映射之外的事:guard page 怎么当越界探测器?W^X 在 Linux 上的真实边界在哪?哪些段咱们动不得?还有一个降权降到自己头上的现场,咱们到 E1 续里再撞它。

实验的编号是 E1 到 E6,与仓库 `code/volumn_codes/vol8/systems-programming/linux/memory/02-vm-apis/` 下的 01 到 06 六个目录一一对应,代码连同全部的原始输出都收在存档里,您随时能对表。正文里的输出块多数是节选,删掉的场景头与说明行以 `...` 标出,拿存档对表的时候请以存档为准。本篇的 E 只认本篇,file-io 那边各篇自己的 E 系与咱们互不相干,您翻存档的时候认目录号就好。系列的老三件 `unique_fd`、`sys_call`、`errno_code` 加上第四件 `mapped_region`,沿用[RAII 篇](../../thinking/01-raii-paradigm.md)与[错误处理篇](../../thinking/02-error-paradigm.md)两篇的定义,本篇咱们只引用、不再重定义。信号 handler 里唯一允许的输出通道是 `write_all`/`write_hex`,它们是裸 write 的两个小封装,而 printf 并不异步信号安全,所以 handler 里碰不得,L02 的 sigbus 一节用过同款,存档把它收进了 `common/sigout.hpp`。触发 SIGSEGV 的动作也沿用 L02 的纪律:fork 一个子进程去干,咱们让 handler 只用 write 打印,而父进程的 waitpid 负责收尸解读。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,编译的口径一律 `-std=c++20 -Wall -Wextra -O2`。有三条环境事实直接决定复跑的成败。E2 的预读实验要一个 ext4 上的 64 MiB 文件,路径写死在了 `/home/charliechen/lm02_scratch/`,复跑之前您得把 `~/lm02_scratch` 建出来,而放在 tmpfs 上预读根本就不成立。本机的 `RLIMIT_MEMLOCK` 是 64 MiB,而软硬限额相同,E3 的边界数字都按它算,zsh 里 `ulimit -l` 报的 65536 是 KiB 单位,您可别读成 64 KiB。输出里的地址全吃 ASLR,而每次运行都会变,咱们引用的规律是 si_addr 等于出错的那个字节这一类等式,而不是任何具体的数值。

## E1:mprotect 深讲,si_addr 的精度、W^X 的边界、动不得的清单

### guard 页上,si_addr 逐字节跟随

咱们从 L02 留下的一个细节审起。那篇的实验里,s2 与 s3 的 si_addr 都报在了页首,而那两次访问打的本来就是页首那个字节,咱们光凭那两次,分不清 si_addr 报的是页首还是出错的那个字节。偏移更细的行为,咱们在这里补上:两页匿名可写映射,咱们把第二页降成 `PROT_NONE`,这就是 guard page(守卫页)了,一页谁碰谁 SIGSEGV 的地址,栈越界探测和分配器隔离用的都是它。然后咱们打它三个不同的字节:第 0 个、最后一个(第 4095 个)、中间偏 123 处,头两下走的是写,第三下换成了读。

```text
== (1) guard page: si_addr follows the exact byte ==
data  page = [0x7ad3a9f31000, 0x7ad3a9f32000)  rw-p
guard page = [0x7ad3a9f32000, 0x7ad3a9f33000)  ---p (PROT_NONE)
   maps: 7ad3a9f31000-7ad3a9f32000 rw-p 00000000 00:00 0
   maps: 7ad3a9f32000-7ad3a9f33000 ---p 00000000 00:00 0
data[kPage-1] = 'K' ok (last legal byte)
...
[handler] SIGSEGV in scenario "g1 write guard+0", si_addr = 0x00007ad3a9f32000, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 71 as expected
...
[handler] SIGSEGV in scenario "g2 write guard+last", si_addr = 0x00007ad3a9f32fff, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 72 as expected
...
[handler] SIGSEGV in scenario "g3 read guard+123", si_addr = 0x00007ad3a9f3207b, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 73 as expected
```

而三个 si_addr 一个都不重样,落点的值分别是 `...2000`、`...2fff`、`...207b`,每一个都精确等于咱们访问的那个字节的地址,没有任何页对齐的取整。L02 的那两次访问打的本来就是页首那个字节,而两种读法在页首上恰好重合,真实的规律是,**si_addr 报的就是出错的那个字节本身**。这个精度直接决定了 E4 里 handler 能算出越界第几字节,咱们整件工具的地基就是它。maps 的输出也顺带看一眼:mprotect 之后,原来的一个映射被撕成了两段,而 `rw-p` 与 `---p` 各自成行,guard 页在内核眼里就是一段独立的 VMA。

### W^X 三步曲:Linux 的放行与 macOS 的拒绝

W^X 是 write XOR execute 的缩写,而写与执行互斥,咱们得正式认识一下:它是安全上的一条主张,同一页内存要么可写、要么可执行,而不许兼得,这样攻击者就没法往可写区塞进机器码再执行了。它也是 JIT(即时编译、把运行期生成的机器码放进内存执行)绕不开的坎,生成的代码总要写进去,接着切换成可执行的权限,改完了再切回来。教科书给的标准做法是三步:RW 写码、RX 执行、改码再回 RW。咱们照着做一遍,x86-64 上写六字节就够一段代码了:

```cpp
// wx_body() 节选:msg 缓冲与 snprintf 的打印脚手架略
unsigned char* code = static_cast<unsigned char*>(
    ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

const unsigned char kRet42[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}; // mov eax,42; ret
std::memcpy(code, kRet42, sizeof kRet42);                 // step1 [RW] 写码
::mprotect(code, kPage, PROT_READ | PROT_EXEC);
const int r42 = reinterpret_cast<int (*)()>(code)();      // step2 [RX] 执行 -> 42

::mprotect(code, kPage, PROT_READ | PROT_WRITE);          // 回 RW 改码
const unsigned char kRet99[] = {0xB8, 0x63, 0x00, 0x00, 0x00, 0xC3}; // mov eax,99; ret
std::memcpy(code, kRet99, sizeof kRet99);
::mprotect(code, kPage, PROT_READ | PROT_EXEC);
const int r99 = reinterpret_cast<int (*)()>(code)();      // step3 [RW->RX] -> 99
```

```text
...
   step1 [RW ] memcpy mov eax,42; ret
   step2 [RX ] mprotect RX, call -> eax = 42
   step3 [RW->RX] patch mov eax,99; ret (x86-64 取指缓存硬件自洽,无需显式 flush), call -> eax = 99
   step4 mprotect(R|W|X) rc=0  <- W^X 只有 macOS 强制,Linux 放行
```

而 step1 到 step3 一次就过了,42 与 99 也都如约到了手,咱们没做任何缓存冲刷,x86-64 的取指缓存在这儿是硬件自洽的,别的架构(比如 ARM)才需要正经考虑 icache 的同步。真正有意思的是 step4:咱们把三样权限一口气全要了,mprotect 回的 rc 是 0。而 Linux 根本不强制 W^X,RWX 在本机的 6.18 内核上是一把过的。macOS 那边的文档口径不同:按 Apple 官方文档的说法,10.14 起对写与执行并存的页有管制,同一步在那边收到的会是 EPERM,不过咱们没在 macOS 上实测,这里记的是文档的说法。放行也有放行的方便:step4 之后咱们往页里写一个 `0xCC`(x86 的 int3 指令),连切换的功夫都省了、当场调用——

```text
   step4' write int3 under RWX, call ->
[handler] SIGTRAP in scenario "wx4 exec int3 under RWX", si_addr = 0x0000000000000000, si_code = 128 (SI_KERNEL)  <- 内核送的 trap,不带精确地址(本机 int3 实测)
```

SIGTRAP 来了,咱们顺带抓到一个文档与现实的错位:man 2 sigaction 给 SIGTRAP 预备的档位里有 TRAP_BRKPT(断点陷阱)一说,而本机的实测下来,int3 送来的 si_code 却是 128(SI_KERNEL),意思是内核送的 trap,而 si_addr 是 0,而且不带任何精确地址。咱们记实测的口径,TRAP_BRKPT 那一档在本机的实测里没露过面,什么时候会露面咱们不猜。

### 动不得的清单:与直觉正好相反

三步曲咱们玩的都是自己的匿名页,那 mprotect 对进程里既有的段是什么态度?咱们把代表段逐一调一遍。咱们的直觉说,text 段加 W 总该被拒绝吧?代码段岂能想写就写?实测的答案一整行一整行地摆在这儿:

```text
...
   text (function page) r-x   -> r-x  rc= 0 (ok)
   text (function page) +W    -> rwx  rc= 0 (ok)
   text page really became rwx -- maps says:
   maps: 61f2a5007000-61f2a5008000 rwxp 00001000 08:30 1689032                    /home/charliechen/lm02_scratch/e1
   rodata page           +W   -> rw-  rc= 0 (ok)
   rodata real write under rw: 'R' -> 'X' (COW copy, no signal)
   .data page           ->r   -> r--  rc= 0, restore rw rc= 0
   .data page           rw    -> rw-  rc= 0, restore rw rc= 0
   heap(brk) page       ->--- -> ---  rc= 0, restore rw rc= 0
   heap(brk) page        +x   -> r-x  rc= 0 (ok)
   heap(brk) page       rw    -> rw-  rc= 0, restore rw rc= 0
   [vvar]                     -> rw-  rc=-1 errno=13 (Permission denied)
   [vdso]                     -> rw-  rc=-1 errno=22 (Invalid argument)
   [vsyscall]                 -> 本机 maps 里没有这段映射
...
```

咱们挨个看下来。头一个试的是 text 加 W,rc 回的是 0,maps 里那页真的变成了 rwxp。咱们还真的往 rodata 里写了一发,`'R'` 改成了 `'X'`,而没有任何信号。直觉里的 EACCES 在加宽这几段的路上没有出现,为什么?因为这些段来自可执行文件的 `MAP_PRIVATE` 映射,加宽权限改的只是页表,真写下去的那一刻走的是写时复制(copy-on-write:写私有文件映射的时候,内核复制出的是一份私有副本,改动永远进不了原文件),咱们写的是自己的副本,而文件毫发无伤。heap 加 x 也放行了,一个可执行的堆就这么立起来了。man 2 mprotect 的 NOTES 对此早有交代:Linux 放宽到进程地址空间里几乎哪儿都能改,除了 vsyscall 区,而 [vsyscall] 在本机的 WSL2 上连映射都不存在,上一篇画全图的时候咱们也没见到它。

真正动不得的另有其人:`[vvar]` 吃了 EACCES,`[vdso]` 吃了 EINVAL。它们俩在上一篇的全图里露过面,是内核映射进每个进程的特殊段:vdso 是一张内核准备的小共享库,gettimeofday 这类调用靠它免掉了一次陷入内核,vvar 则是它配套的数据页。而内核没有把它们当普通页看待,mprotect 一伸手就被挡了回来,而且挡法还不一样,一个说的是权限不能这么给,另一个说的是参数本身就不合法。另外咱们记一笔分寸:rc=0 只说明改成了,真访问了才见分晓。存档里 seg5/6/7 三个子进程把降权后的 .data、heap、stack 页各踩了一次,送来的都是 SEGV_ACCERR,与 L02 的分工一致。seg7 还多一层讲究:被降权的那页就住在栈上,handler 干脆换到了 `sigaltstack` 备好的另一块栈上落脚。探针挑的是旧帧页,而 altstack 在咱们这儿是保险,而不是非它不可。

## E1 续:降权 .data,踩着 ld.so 的懒解析

加权放行得这么痛快,那降权呢?咱们把 .data 页从 rw 降到只读,然后做一件再普通不过的事:读一次 errno。下面的一幕,笔者第一次跑出来的时候盯着屏幕愣了半天。

```text
-- scenario: seg8 cold errno read under RO .data -> SIGSEGV inside ld.so (GOT lazy binding)
    seg8: child mprotect(.data page, PROT_READ) rc=0, now first cold errno read

[handler] SIGSEGV in scenario "seg8 cold errno read under RO .data page", si_addr = 0x000061f2a500c000, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 78 as expected
   (&g_data = 0x61f2a500c0d8, si_addr 与它同页:那里住着 .got.plt)
...
```

死因不在咱们的代码里,咱们得把两位当事人请出来。GOT 是 Global Offset Table 的缩写,中文的名字叫全局偏移表,它是动态链接用来登记外部符号真实地址的一张表。PLT 是 Procedure Linkage Table 的缩写,中文的名字叫过程链接表,它是每个外部函数入口前的一小段跳板。动态链接默认开的是懒解析(lazy binding):头一次调用某个外部函数的时候,GOT 里那个槽还是空的,PLT 把咱们带进 ld.so 的解析器,这里的 ld.so 说的就是动态链接器,上一篇 maps 里 ld-linux 那 5 段的主人正是它。解析器查到地址,**写进了 GOT**,下次的调用才直达。麻烦就出在这一写上:本机的 `.got.plt` 和 `.data` 挤在同一页。咱们把那页降成只读之后,任何本进程第一次调用的 libc 函数,解析器替咱们写 GOT 的那一下,就被 SIGSEGV 当场按住了,进程死在了 ld.so 手里,而不是死在咱们的变量上。`errno = 0` 这一句展开成了 `*__errno_location() = 0`,实验的 prewarm 阶段把 write 和 strlen 都提前调热了,于是这场死亡精确锁定在 errno 这个冷符号的解析上,si_addr 指的正是 GOT 所在的页首。

写 handler 的时候咱们也有讲究,而代码就两行,咱们原样看:

```cpp
// e1.cpp 的 on_signal 节选
void on_signal(int sig, siginfo_t* info, void*)
{
    // 进 handler 第一件事:把 .data 页权恢复。mprotect 异步信号安全,且早已解析。
    // 不然 handler 自己要用的函数谁还没解析过,就要写 GOT——而 GOT 与 .data 同页,
    // handler 会当场二次 SIGSEGV,被默认动作直接带走
    ::mprotect(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(&g_data) & ~(kPage - 1)),
               kPage, PROT_READ | PROT_WRITE);
    write_all("\n[handler] ");
    // ... 打印 sig / si_addr / si_code 后 _exit(70 + g_which)
}
```

咱们的 handler 要开口,就可能路过没解析过的函数。所以这两行的顺序不能倒:开口之前就把页权恢复了,mprotect 本身异步信号安全而且早已解析。这一幕还有两个延伸的枝节。复跑的角度:seg8 依赖进程还没读过 errno 的冷状态,复跑请保持存档里的原顺序,把链接选项换成 `-Wl,-z,now` 全急解析的话,GOT 在启动时就填满了,这一幕就演示不出来了。工程的角度:上一篇全图里每个文件模块都有的那段紧贴 rw 的只读 GNU_RELRO,干的正是同方向的事,重定位做完了就把 GOT 转成只读,而谁都不许再写。咱们这一幕把 GOT 所在页降成只读,方向与 RELRO 是一致的,真正开了口子的另有其人:handler 进门的第一件事,就是把这一页改回了可写,懒解析的路跟着又通了。RELRO 想堵的正是自动松权的那扇门,而页权这件事,松一档就有松一档的代价。

## E2:madvise:把用法递给内核

mprotect 下的是命令,而内核必须照办。madvise 给的却是建议,而内核听不听全看它自己。它的签名就一行 `madvise(addr, length, advice)`,advice 的取值从一串 `MADV_` 常量里挑。man 2 madvise 把话说得很清楚:常规的建议只影响性能,唯独 MADV_DONTNEED 是会改变语义的例外,咱们到 (B) 就知道它改得多狠。

### (A) 预读策略:三个数字各说各话

咱们在这里把预读(readahead)说清楚:您摸了文件的一个页,内核会自作主张地把后面好几个页也读进页缓存,赌您马上要用。赌对了省一堆缺页,赌错了就白烧 IO。咱们递给内核的提示,不外乎 NORMAL、SEQUENTIAL、RANDOM 三档的名字。测量办法是:E2 在 ext4 上备了 64 MiB 文件,每轮咱们都用 `posix_fadvise(DONTNEED)` 把页缓存压干净,映射之后只摸头部的 1 MiB(每页一个字节),再用 mincore 数驻留涨了多少(mincore 是什么,咱们到 E5 再细说)。咱们把三轮数字一并摆出来,主档之外再加两次复跑的数字:

| MADV 策略 | 主档 | 复跑一 | 复跑二 |
| --------- | ---- | ------ | ------ |
| NORMAL(自适应) | 8192 KiB | 8192 KiB | 3088 KiB |
| SEQUENTIAL | 3084 KiB | 3088 KiB | 3080 KiB |
| RANDOM | 1024 KiB | 1024 KiB | 1024 KiB |

RANDOM 一列稳得漂亮:咱们摸了 1 MiB,而驻留恰好是 1 MiB,多的一页都没有,预读被整个关掉了。偏偏随机访问本来就喂不饱预读,所以不喂也罢。而 SEQUENTIAL 稳稳守在 3080 KiB 一档,窗口是固定的,给了也就多两倍。最让笔者意外的是 NORMAL:主档 8192 KiB,复跑二却掉到了 3088 KiB,同一段代码给出了两种答案。而这不是测量误差,readahead 的窗口是自适应的,内核跟着访问的历史调,咱们存档连 rerun 一起收了三组,就是为了让您亲眼看到它抖。所以工程上引用 NORMAL 的预读量,谁报一个固定常数您都别信,咱们这里的表也一样。另有一位不要建议、直接下命令的:

```text
   MADV_WILLNEED (8 MiB, no touch):
   before        :    0 KiB resident
   after WILLNEED: 8192 KiB resident (prefetch is async; polled until stable)
```

MADV_WILLNEED 咱们一页没摸,8 MiB 自己进了页缓存。不过实验里注明了 async:预读是异步的,咱们轮询 mincore 到数字稳定才读的数,您自己跑的时候也请等一下,不然读到的多半是还没稳定的数。

### (B) MADV_DONTNEED:匿名页读回零,数据没了

DONTNEED 单独是 man 页盖章的语义例外。咱们拿 8 MiB 匿名页写满 `0xC0DE0000+i` 的花纹,然后对前面的 4 MiB 说 DONTNEED:

```text
...
   wrote 8 MiB pattern (0xC0DE0000+i), Rss = 8192 kB
   p[0]=0xc0de0000, p[1MiB]=0xc0e20000, p[5MiB]=0xc0f20000
   madvise(first 4 MiB, MADV_DONTNEED) rc=0, Rss = 4096 kB
   read back: p[0]=0x00000000 (zero page), p[1MiB]=0x00000000, p[5MiB]=0xc0f20000 (intact)
   mincore first 64 KiB: 1000000000000000
   mincore last 64 KiB:  1111111111111111
```

Rss 从 8192 kB 落到了 4096 kB,物理页真的还了。咱们再读 p[0],回来的全是零,花纹没了。man 页的原话是后续访问会拿到 zero-fill-on-demand pages,内核没有保存副本的义务,它就是这么执行的。位图第一格那个孤零零的 1,咱们到 E5 再解释,那是零页口径的事。所以 DONTNEED 不是无损的释放,它拿数据换内存:缓存可以丢、可以重算的数据,咱们换得起,而计算结果换不起,所以别递这个建议。文件页那边的对称操作,L02 讲过它的分工是共享映射重读文件、私有映射丢掉 COW 副本,而 E5 还会再给它补一刀。

### (C) MADV_DONTFORK:fork 之后,子进程里这段没了

咱们都知道 fork 的时候子进程会继承整个地址空间,而 MADV_DONTFORK 偏要让指定的段例外。实验里咱们让父进程备了两页,一页挂上了 DONTFORK,另一页当作普通的对照,咱们都写上花纹再 fork:

```text
...
   parent: DONTFORK page 0x7e94794f6000 = 0x5a5a5a5a, COW page 0x7e94794f5000 = 0xa5a5a5a5
   child : DONTFORK page in maps? NO (gone) ; COW page in maps? yes
   child : COW page reads 0xa5a5a5a5, child writes 0x11 -> reads 0x00000011

[handler] SIGSEGV reading DONTFORK page, si_addr = 0x00007e94794f6000, si_code = 1 (SEGV_MAPERR)  <- 子进程里地址根本不在映射里
   child exited 83; parent after: DONTFORK page = 0x5a5a5a5a, COW page = 0xa5a5a5a5 (COW 各改各的)
```

子进程的 maps 里,那一页整段没了。读它送来的是 SEGV_MAPERR,地址根本不在映射里,和踩 PROT_NONE 页的 SEGV_ACCERR 正好区分开。对照页一切如常:子进程读到了父进程写下的值,再写自己的 COW 副本,而父子互不干扰。这东西给谁用?man 页点的是 DMA 的硬件,DMA 是 direct memory access(直接内存访问)的缩写,说的是设备绕过 CPU、直接读写物理内存的那类数据搬运。COW 搬家会把页挪到新的物理地址,直接往老地址 DMA 的硬件就找不着人了。而 DONTFORK 让子进程干脆看不见这段,主人换人的麻烦就没了。写多进程 RDMA(remote DMA)类程序的朋友们,远程的直接内存访问说的就是它,以后还会再遇见它的。

### (D) MADV_REMOVE:ext4 也真打洞

老一点的资料把 MADV_REMOVE 说成 tmpfs/shmem 专属,现在的 man 页已经改了口,说 Linux 3.5 起任何支持 fallocate 的 `FALLOC_FL_PUNCH_HOLE` 模式的文件系统都接它,而它的本质就是把那段映射换成 fallocate 打洞。当然,纸面上的说法咱们已经听到了,接下来咱们在 ext4 上动手验:

```text
...
   anon private : rc=-1 errno=22 (Invalid argument)
   ext4 shared  : 3 pages written; madvise rc=0 errno=0; pread middle all-zero; st_blocks 24 -> 16, size=12288 (hole punched, KEEP_SIZE)
   tmpfs shared: 3 pages 'a','b','c'; size=12288, st_blocks=24 (512B units), shm f_bfree=6940363
   madvise(middle page, MADV_REMOVE) rc=0 errno=0
   after punch: size=12288 (unchanged); st_blocks 24 -> 16 (page freed), then read hole via mapping -> st_blocks back to 24 (shmem 立了零页)
   map page0='a' page1=0x00 page2='c'; pread middle page reads all-zero (hole)
```

私有匿名段拿到的直接是 EINVAL,它背后没有可以打洞的东西。ext4 的共享映射上,咱们把三页写满 abc,对中间页下了 REMOVE 之后,`pread` 读回来的全是零,st_blocks 从 24 落到了 16(st_blocks 的单位是 512 字节,而一页正好是 8 个),而文件大小却纹丝不动,洞是打实了。tmpfs 上咱们还多看了一眼后续:洞经映射再读一下,shmem 给这页又立了一个零页,占块数又弹回了 24。所以打洞是真打,而读洞会重新花钱,花钱的道理,和 E5 的口径分叉是同一路的。

## E3:mlock:把页留在物理内存里

第三个 API 管的是驻留。内核在内存吃紧的时候会把不活跃的页换去 swap,换来的就是那次访问毫秒级的磁盘延迟,对做实时音频的咱们来说,这延迟到了耳朵里就是爆音。而密钥页要是被换出去,内容就落到了交换区,这就是泄露面了。mlock 的承诺写在 man 2 mlock 里:调用成功返回时,这段地址覆盖的所有页保证都在 RAM 里,而且解锁以前一直都在。咱们把这层拆成四个可观察面,一个一个地量过去。

头一个观察面就带着惊喜:mlock 自己把缺页全干了。咱们备一段 8 页的匿名映射,而且一个字节都没写过,mincore 的位图是 `00000000`。咱们一调 `mlock(p, 2 * kPage)`,位图变成了 `11000000`,`VmLck` 与 smaps 的 `Locked` 双双涨到 8 kB:

```text
   before mlock  bitmap: 00000000
   VmLck=0 kB, smaps Locked=0 kB
   mlock(p, 2 pages) rc=0  -- pages still never written by us:
   after mlock   bitmap: 11000000
   VmLck=8 kB, smaps Locked=8 kB  <- mlock 自己把页摸进了内存
   p[0]=0x7f written; VmLck=8 kB (unchanged)
...
```

咱们细想一下,它其实不算副作用:承诺说的是成功返回即驻留,那咱们要是不预故障,又怎么兑现承诺?所以 mlock 顺手把两次缺页提前付掉了。您要是不想让它现在就摸,内核另备了 `mlock2()` 加 `MLOCK_ONFAULT` 的变体:已经在内存里的页照旧锁上,还没进来的页,落进来一次就锁住了不再走。第二个观察面是 munlock 的分寸:解锁之后位图仍是 `11000000`,而页一页都没走,`VmLck` 归了零。解锁收回的是不许换出的承诺,而不是把页请出去,逐出的时机由内核看着内存的压力定。

而第三个观察面说的是额度,本机的 `RLIMIT_MEMLOCK` 是 64 MiB,咱们贴着线量:

```text
...
   mlock(limit + 1 page = 67112960 B): rc=-1 errno=12 (Cannot allocate memory)
   mlock(limit exactly = 67108864 B): rc=0 errno=0 (Success)
   VmLck=65536 kB
   one more page on top of a full limit: rc=-1 errno=12 (Cannot allocate memory)
...
```

恰好等于限额的那次,成功了。多要一页的那次,回的是 ENOMEM,而 errno 12 的字面意思是内存不够,在这儿真正的含义是锁的额度用完了,报错的文案和病因对不上号,您调试的时候别被它带偏。锁满之后咱们再想补一页,拿到的同样是 ENOMEM,额度按累计的锁定量算,而不是按单次调用算的。而 man 页还注明,2.6.9 起的软限额只管非特权进程,有 CAP_IPC_LOCK 的进程不受限。第四个观察面说的是 mlockall:`mlockall(MCL_CURRENT)` 一句把当前地址空间全锁了,本进程的 RSS 才 4 MB,而它远在限额之内,rc 回的是 0,`VmLck` 涨到了 7296 kB。最后要提的还有 `MCL_FUTURE`,它锁的是将来的映射,man 页给它配了警告:之后的 mmap、sbrk 可能因为额度直接失败,栈增长要是失败了连错误码都没有,送的是 SIGSEGV,没有把握您就别开这一档。

本机的 swap 现状咱们如实记下:/proc/swaps 里 16 GiB 的 /dev/sdc 一页未用,而 priority 与 swappiness 分别是 -2 和 60。锁定的页永不换出,这是 mlock 存在的全部理由,而这句话在本机上没法确定性演示:逼内核换页需要 root 动 drop_caches,或者制造危险的水位压力,实验存档里咱们按原样记了档,咱们没编造现场。

## E4:guarded_buffer<T>:越界第几字节,当场报出来

三个 API 各自的本事看完了,咱们把它们攒成一件趁手的工具。咱们把问题摆在这儿:`char d[100]` 放在普通的堆上,然后咱们写 `d[100] = 'X'`,越界的就是一个字节,您猜会发生什么?多半什么都不发生,写进了邻居的字节里,而且一声不吭。ASan(AddressSanitizer:编译器插桩的内存错误检测器)倒是能抓,但咱们今天手搓一个原理版的:让 d[100] 那个字节本身就不存在。布局要诀一句话:您要 N 字节,咱们给 `ceil(N/page)` 页数据加 1 页 PROT_NONE 的 guard,然后**把数据的末字节对齐到 guard 页的页首**。

```cpp
// e4.cpp 的 guarded_buffer 节选:错误分支打印从略
template <class T>
class guarded_buffer {
public:
    explicit guarded_buffer(std::size_t n)
        : n_(n), bytes_(n * sizeof(T)),
          payload_((bytes_ + kPage - 1) / kPage * kPage)
    {
        base_ = static_cast<unsigned char*>(
            ::mmap(nullptr, payload_ + kPage, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base_ == MAP_FAILED) { std::printf("mmap failed\n"); std::abort(); }
        guard_ = base_ + payload_;                       // 尾页整页降权
        if (::mprotect(guard_, kPage, PROT_NONE) != 0) { std::abort(); }
        data_ = reinterpret_cast<T*>(guard_) - n;        // 数据末字节紧贴 guard 页首
    }
    ~guarded_buffer() { ::munmap(base_, payload_ + kPage); }

    guarded_buffer(const guarded_buffer&) = delete;
    guarded_buffer& operator=(const guarded_buffer&) = delete;

    T* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return n_; }
    // guard_begin()/guard_end() 交给 handler 做区间对照,略
private:
    std::size_t n_, bytes_, payload_;
    unsigned char* base_ = nullptr;
    unsigned char* guard_ = nullptr;
    T* data_ = nullptr;
};
```

为什么非得尾对齐?咱们反过来摆一下就明白了:要是数据从页首对齐放,100 字节的后面还剩着 3996 个合法字节,它们其实全躺在同一页里,咱们写 `d[100]` 舒舒服服,而 guard 页还远在天边,结果一个字节都没报。而尾对齐之后,`data[N]` 恰好是 guard 页的第 1 字节,越界了多深,就会落进 guard 里相应的深度,而 E1 刚好给咱们验过,si_addr 是逐字节精确的。handler 这就有了发挥的空间:咱们拿 `si_addr - guard_begin + 1` 一减,越界第几字节就自己报出来了。跑给您看:

```text
== construct guarded_buffer<char>(100) ==
   data  = [0x71d77caf1f9c, 0x71d77caf2000) 100 bytes, 末字节紧贴 guard 页首
   guard = [0x71d77caf2000, 0x71d77caf3000) PROT_NONE
...
== overflow by exactly 1 byte: d[100] = 'X' ==
[handler] SIGSEGV, si_addr = 0x000071d77caf2000, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf2000, 0x000071d77caf3000), si_addr IN guard, 越界第 1 / 4096 字节; requested address = 0x000071d77caf2000 == si_addr (精确落点)
   caught it, process alive, d[99] still 'Z'
== overflow 1 page deep: d[100+4095] ==
[handler] SIGSEGV, si_addr = 0x000071d77caf2fff, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf2000, 0x000071d77caf3000), si_addr IN guard, 越界第 4096 / 4096 字节; requested address = 0x000071d77caf2fff == si_addr (精确落点)
   caught at guard's last byte
== overflow READ: c = d[100+3] ==
[handler] SIGSEGV, si_addr = 0x000071d77caf2003, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf2000, 0x000071d77caf3000), si_addr IN guard, 越界第 4 / 4096 字节; requested address = 0x000071d77caf2003 == si_addr (精确落点)
   reads are guarded too
...
   di[9]=81 ok
[handler] SIGSEGV, si_addr = 0x000071d77caf0000, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf0000, 0x000071d77caf1000), si_addr IN guard, 越界第 1 / 4096 字节; requested address = 0x000071d77caf0000 == si_addr (精确落点)
   caught: di[10] faults at guard start
== underflow d[-1]: NOT covered by a rear-only guard ==
   no signal -- 前向不设防,这就是本设计的边界(前后双 guard 可补)
...
== control group: same layout WITHOUT guard page ==
   ctrl buffer at 0x71d77caedf9c, ctrl[100] lands at 0x71d77caee000 (page 2, rw-p)
   ctrl[100]='X' -- no signal, value read back: 'X' (silent corruption)
   ctrl[100+4095]='Y' -- still no signal
...
```

咱们把输出里最值钱的几行挑出来念。`d[100]` 只越界了一个字节,而 si_addr 与请求地址完全相等,handler 报出了越界第 1/4096 字节。越界到一整页深的那次,handler 就报出了第 4096/4096。读越界的同样拦,报的是第 4/4096。轮到 `guarded_buffer<int>` 的版本,`di[10]` 是一个 4 字节的 int,它的首字节就是 guard 首字节,结果它照拦不误。d[99] 倒是一直守着 'Z',合法区的一个字节都没伤着。两处边界咱们也摆在明处:guard 只有 1 页的深度,越界超过 4096 字节就跨出去了,那就不归它管了。下溢 `d[-1]` 落在前面的数据页里,那里是活的映射,偏偏没有任何信号,单向的 guard 管不着负方向,前后各立一页的双 guard 版可以补,改动留给您当练习。

对照组咱们也没省:同款布局、不设 guard 的映射,咱们同样越界 1 字节和 4095 字节,全都无声地通过了,而且值还读得回来,这就是静默写脏的下场了。其实还有一个工程细节值得看:handler 没有让进程死,`sigsetjmp`/`siglongjmp`(savemask=1)把执行流接回了主流程,后面的几幕实验在同一个进程里接着演。分配器隔离带、Electric Fence 那一代的调试分配器,用的正是这一手:把每次分配的末尾贴上 PROT_NONE 的页,越界就从静默写脏变成了必炸且带定位。mmap 拿到的那几页、mprotect 降权的 guard 页、handler 报出的越界位置,这件工具的三块积木到这儿就齐了。

## E5:mincore:一张驻留位图,三种口径

E2 和 E3 咱们已经反复用它量数了,现在把它的签名摆出来:`mincore(addr, length, vec)` 给范围内的每一页在 vec 里留一个字节,man 2 mincore 的说法是,当最低位是 1 的时候,代表的正是这一页 currently resident in memory,而再摸不会碰盘。这句话的字面平平无奇,咱们把一张匿名映射的一生拍成一组连续快照:

```text
== anonymous 8 pages, page by page ==
   1) fresh mmap, untouched:          00000000  (0/8 resident, Rss=20 kB)
   2) wrote pages 0,3,7:              10010001  (3/8 resident, Rss=32 kB)
   read page 1 (never written) -> 0x00000000
   3) after READ of page 1:           11010001  (4/8 resident, Rss=32 kB)
   4) after DONTNEED page 3:          11000001  (3/8 resident, Rss=28 kB)
   5) after READ page 3:              11010001  (4/8 resident, Rss=28 kB)
...
```

第 3 行是本篇笔者最喜欢的一处反常:咱们只读了第 1 页,而且从没写过它,位图上它变 1 了,而 Rss 却一格没涨。这就要说到零页(zero page)了:内核备了一张全零的共享只读页,匿名映射里没写过的页,读的时候全都映射到它身上,谁家都不用真的分一页内存。mincore 数的是 PTE(page table entry,页表项)的 present 位,而零页也算 present,可 smaps 的 Rss 只数真分给本 VMA 的物理页,共享的零页不算。所以同一次读,咱们就得到了两个仪器的两个答案,这就是口径的分叉,咱们把两个数都记下,而不替它们裁决谁对谁错。第 4、5 行接着呼应 E2 的 (B):DONTNEED 之后位图归了 0,咱们再读,零页又把 present 位立回来了。

文件页的那边还有第二种口径。咱们备 2 页的文件映射,页缓存压干净了就 WILLNEED 预读,而再逐段丢弃:

```text
== file-backed 2 pages (ext4) ==
   1) after fadvise DONTNEED:         00  (0/2 resident, Rss=0 kB)
   2) after MADV_WILLNEED:            11  (2/2 resident, Rss=0 kB)
   page 0 reads 0xaaaaaaaa (now faulted into OUR page table, Rss grows)
   3) after reading page 0:           11  (2/2 resident, Rss=8 kB)
   4) DONTNEED page 1:                11  (2/2 resident, Rss=4 kB)
   5) fadvise DONTNEED again:         10  (1/2 resident, Rss=4 kB)
...
```

咱们看第 2 行,mincore 全 1 而 Rss=0:文件映射的 mincore 问的是页缓存,页缓存里有了它就报 1,哪怕这一页还没缺页进本进程的页表。而 man 那句再摸不碰盘,在这里是成立的,不过括号里把 page fault 与 disk access 并排写,实测的口径是页还是要缺一次的,只是缺的这一次不碰盘。第 4 行从文件页那一侧又验证了一遍:咱们对文件页 DONTNEED,丢的只是本进程的页表映射,页缓存里的文件页还在,所以 mincore 依旧 1。咱们真要把文件页请出页缓存,要靠 `posix_fadvise`(文件级)的通道,结果第 5 行它来了,可也只请走了一页:page 0 还映射在本进程页表里,它受着 PTE 的保护,所以页缓存请不动它。而 page 1 没有映射,请走了。所以 mincore 的那个 1,至少有三种来源:PTE 的 present(零页也算)、页缓存里有,以及真的驻留。您拿它对表的时候,旁边请永远站一个 smaps 的 Rss,不然这个 1 就说不清自己是什么了。

## E6:process_vm_readv:跨进程读一眼

最后一个 API 咱们一段带过,它是整个全景的边角,不过它常常被人漏掉。进程 A 想读进程 B 的一段内存,从前的老办法是 ptrace,而 Linux 3.2 起有了专职的只读通道 `process_vm_readv`,本地和远端各一组 iovec:

```cpp
// e6.cpp 节选:只留调用三行
struct iovec local_iov { local, sizeof local };
struct iovec remote_iov { g_payload, sizeof g_payload }; // fork 继承,地址同值
const ssize_t n = ::process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
```

```text
parent read child's memory: 32 bytes ->
   child  has [0xc0de0000 0xc0de0001 .. 0xc0de0007]
   parent has [0x00000000 0x00000001 .. 0x00000007] (untouched, COW)
read unmapped remote addr : rc=-1 errno=14 (Bad address)
read bogus pid            : rc=-1 errno=3 (No such process)
```

fork 之后子进程就把自己的全局数组改成了 0xC0DE 花纹,父进程的副本被 COW 保护着、一动不动,而一句 process_vm_readv,就把子进程那 32 字节抄了回来。错误路径的表现也值得看一眼:远端地址要是没映射,咱们收到的是 EFAULT(14)。而 pid 要是不存在,收到的是 ESRCH(3)。man 页写明权限检查走的是 ptrace 的同一套(PTRACE_MODE_ATTACH_REALCREDS),同 uid 或者有特权的人才行。而它还不保证原子,返回的值可能小于请求量,页粒度的部分传送是存在的,用的时候请您检查返回值。崩溃收集器、调试器拿它做的都是只读侦察。咱们真要进程之间正儿八经地共享数据,靠什么?主力仍然是共享内存,那是下一篇的正题。

## 另一侧怎么看

Windows 这边的镜像是 VirtualProtect,PROT_NONE 对应的是 PAGE_NOACCESS,咱们的 guarded_buffer 原样能搬过去。但 Windows 还多一档咱们没有的玩法:PAGE_GUARD,一次性的陷阱。挂在 guard 位上的页,它在第一次被访问的时候,会送出 STATUS_GUARD_PAGE_VIOLATION 的异常,然后 guard 位就自动摘掉了,这一页就变成普通可访问页了,而下一次访问畅通无阻。咱们要是想再要一次陷阱,您得重新调 VirtualProtect 把位挂回去。这和 PROT_NONE 的持续拒绝是两种脾气:咱们本篇的 guard 页,第一万次的访问仍然当场 SIGSEGV,而 PAGE_GUARD 却是响一声就放行。Windows 的内核自己就用它做栈扩展的探测。落到 guarded_buffer 上的对照,PAGE_NOACCESS 版的行为与 E4 的一致,PAGE_GUARD 版则要考虑异常之后重新挂位的循环。这趟对照的完整展开,咱们留在 Windows 内存那一篇,它的名字是[VirtualAlloc 与 VirtualProtect](../../windows/memory/01-virtualalloc.md)。VirtualAlloc 的粒度与保留/提交的两段式都在那边等您,而文件后备的 SEC_RESERVE 版本,咱们的实测放在了[文件映射那一篇](../../windows/file-io/02-file-mapping.md)。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="mprotect(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mprotect.2.html"
  />
  <ReferenceItem
    :id="2"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="3"
    title="mlock(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mlock.2.html"
  />
  <ReferenceItem
    :id="4"
    title="mincore(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mincore.2.html"
  />
  <ReferenceItem
    :id="5"
    title="process_vm_readv(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/process_vm_readv.2.html"
  />
  <ReferenceItem
    :id="6"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
  <ReferenceItem
    :id="7"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
