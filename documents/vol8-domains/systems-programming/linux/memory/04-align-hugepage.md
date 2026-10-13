---
title: "对齐分配与大页:32 字节的对齐、2 MiB 的页与一次被拒的 512 GiB"
description: "内存管理章 Linux 侧的收官篇:分配器递出地址之后还剩两件事,对齐与大页。实测对齐三件套 aligned_alloc/posix_memalign/对齐 new 在 align=64/4096 的全部采样里达标,报错口径却分了三家(NULL+errno、EINVAL 当返回值、bad_alloc 异常,glibc 2.44 按 C17 的放宽口径收下了 size 非倍数)。对齐的物理代价给出两条证据:未对齐地址喂 _mm256_load_si256 立即 SIGSEGV,si_code=SI_KERNEL、si_addr 无意义,这是 #GP 而非缺页,vmovdqa 与 vmovdqu 的反汇编就在存档里,同一条缓存行上的两个计数器 529.0 对 74.4 毫秒,7.11 倍。THP 一节是本篇的重头:出厂状态下 MADV_HUGEPAGE 只拿到 AnonHugePages=0,顺着诊断链摸出 WSL2 的 init 给整个进程树设了 MMF_DISABLE_THP,这是压过 sysfs 的进程级第三层开关,prctl 清掉之后 1 GiB 全部大页化,首触写快 5.43 倍而全量 memset 只剩 1.02 倍。显式大页 MAP_HUGETLB 三档全是 ENOMEM,池子是空的,预留要 root。Overcommit 的数字链是 512/256/128 GiB 全被拒、64 GiB 成功,启发式口径下本机实测的拒绝线约在 RAM 加 swap 的 69 GiB,MAP_NORESERVE 让同一份 512 GiB 立刻成功,VmSize 涨满而 VmRSS 不动,另有 gcc -O2 把只做空判的 malloc/free 整对删掉、差点记出 256 GiB 假成功的测量教训。最后的 OOM Killer 一节用只读观察加 touch 60% 的安全演示给 Linux 侧收尾:oom_score 基线 666 的相对值语义、oom_score_adj 只许自损不许自保的 EACCES 边界、dmesg 全量扫描零条 OOM 记录"
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 17
prerequisites:
  - "mmap 内存映射:把文件贴进地址空间"
  - "错误处理范式:从 errno 到 expected"
related:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 内存管理
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 对齐分配与大页:32 字节的对齐、2 MiB 的页与一次被拒的 512 GiB

内存管理这一章的 Linux 侧,咱们已经走过了[布局篇](./01-memory-layout.md)的 maps 全图、[虚拟内存 API 篇](./02-vm-apis.md)的 mprotect 与 madvise,再到[共享内存篇](./03-shm.md)的跨进程映射。malloc 的分水岭在布局篇里量过了,小块走的是 brk,大块走的是 mmap。可分配器把一块地址递到咱们手上之后,其实还剩两样它不替咱们拿主意的东西。头一样是地址本身:它落在 16 的倍数上,还是 64、4096 的倍数上?这就是**对齐**(alignment)。另一样是页的尺寸:虚拟内存按页来管理,咱们嘴上习惯说 4 KiB 一页,可 x86-64 的页表,认得的还有 2 MiB 与 1 GiB,这两档就是所谓的**大页**(huge page)了。

这两样东西的收益都长在硬件上。对齐伺候的是指令与缓存:SIMD 指令集里有一批要挑地址的指令,您要是没把地址对齐,它们当场就罢工了,而缓存行是 64 字节的单元,两个线程要是挤在了同一行上,性能的损失能到几倍。大页伺候的是页表与 TLB(缓存地址翻译结果的那块硬件,中文常叫翻译缓存):页变大了,它的每条记录管的地界就越大,首触的缺页次数也越少。听起来都挺美的,可它们其实值多少,咱们不空口评估,咱们全部用实验来量。讲完了这两块,咱们顺着大页的话题再往下走一层:malloc 成功了,物理内存真的给咱们留了吗?这一问的答案牵出 overcommit(允许承诺超出物理内存的口径)与 OOM Killer(内存见底时内核出来杀进程的机制),它们都在本篇的最后两节,内存的旅程到那儿也就走到了头,正好给 Linux 侧收了尾。

咱们把实验排成了 E1 到 E6 六个实验,与仓库 `code/volumn_codes/vol8/systems-programming/linux/memory/04-align-hugepage/` 下 01 到 06 的六个目录一一对应,代码与全部原始输出都入了册,您随时能对表。环境的口径照例交代清楚,后面的数字都得拿它对表:台机用的 CPU 是 AMD Ryzen 7 9700X,系统跑的是 WSL2,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,glibc 的版本是 2.44,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra -Wpedantic`,拿到的警告数是零。内存的 MemTotal 约 53 GiB,另加了 16 GiB 的 swap。三条系统状态直接决定后面每个实验的走向:THP 的 sysfs 开关停在 `[madvise]` 档,显式大页的预留池是零,overcommit_memory 的值是 0。权限的边界也有一条,咱们得如实交代:笔者的 sudo 要密码,sysfs 与 sysctl 一类的写入操作全都做不了,所以本篇的实验全按无特权用户的真实能力来设计,做不了的部分咱们就明说。`unique_fd`、`sys_call`、`errno_code` 三件公共工具沿用[RAII 篇](../../thinking/01-raii-paradigm.md)与[错误处理篇](../../thinking/02-error-paradigm.md)的定义,本篇的实验以小程序为主,失败码咱们直接打出来,不再额外地包一层了。

## E1 对齐分配三件套:同一种要求,三种报错的走法

咱们从接口看起。C 侧给对齐分配备了两件,C++17 又添了一件:

```c
void *aligned_alloc(size_t alignment, size_t size);
int   posix_memalign(void **memptr, size_t alignment, size_t size);
```

```cpp
void* operator new(std::size_t size, std::align_val_t alignment);
```

三个签名都带着 alignment 的参数,合法的取值有一份共同的要求:**二次幂**,也就是 2 的整数次方,是 1、2、4、8、16、32、64、4096 这样一路翻倍上去的数,48 或 3 这样的取值都不合格。posix_memalign 还多了一条,alignment 也得是 sizeof(void*) 的倍数,x86-64 上就是 8 的倍数,所以传 4 给它同样是非法的。这些要求写在了 man 3 aligned_alloc 与 man 3 posix_memalign 里,咱们按文档把非法值也一并排进实验,看它们各自怎么报错。

E1 的采样咱们铺得很匀:对齐挑的是 64 与 4096 两档,尺寸挑的是 64、100、4096、10000 四种,其中 100 与 10000 故意不是 64 的倍数,三件工具咱们轮着来一遍。全部的采样里,返回地址的 `addr % align` 都是零,完整的输出有点长,咱们摘一段:

```text
==== align = 4096 ====
aligned_alloc(4096,    64)         -> 0x5c328280b000  %4096 = 0  [OK]
posix_memalign(&p, 4096,    64)    -> rc=0(0=success)
  address                          -> 0x5c328280b000  %4096 = 0  [OK]
operator new(   64, align_val_t(4096)) -> 0x5c328280b000  %4096 = 0  [OK]
```

地址本身的达标没什么悬念,真正值得咱们看的,是失败的一面。咱们挨个喂非法的对齐值,三件工具交回来的东西是这样的:

```text
aligned_alloc(48, 128)    -> (nil) errno=22(Invalid argument)
posix_memalign(&p,48,128) -> rc=22(Invalid argument) p=0x1
operator new(8, align_val_t{3}) -> 抛 std::bad_alloc: std::bad_alloc
```

您看,同一个 EINVAL(22) 就走出了三条完全不同的路。aligned_alloc 交回的是 NULL,细节被放进了 errno,咱们拿[错误处理篇](../../thinking/02-error-paradigm.md)的 `errno_code()` 一装箱就能查。posix_memalign 是不碰 errno 的,man 3 的 RETURN VALUE 写得干脆:错误码直接作为返回值交了回来,errno 的值不会被设置。第三件的口径又不一样,对齐 new 失败的时候,C++ 这边的约定是抛 std::bad_alloc,异常对象里揣的只有一句 `std::bad_alloc`,EINVAL 的细节在哪儿都查不到。报错的体系,咱们在错误处理篇里讲过两套:一套走的是 errno 槽位,一套走的是异常。错误码直接当返回值的这一套,是 POSIX 传统的另一支,咱们今天头一回在现场遇上,这里三件工具正好把三路排成了一排,算是给那一篇补了个现场。还有个挺有意思的小细节,咱们传给 posix_memalign 的指针,被咱们提前故意填成了 `(void*)0x1`,失败之后它读回来的值还是 0x1:失败时它不动您传的指针,POSIX.1-2008 TC2 起把这一点写进了规范。

非法的对齐值算一类,size 不是 alignment 的倍数算另一类,C 语言的老规范把后者也算未定义行为。C17 把该要求删掉了,man 3 aligned_alloc 的 NOTES 里记着这段来龙去脉:缺陷报告 N2072 认为 size 必须是 alignment 倍数的要求是多余的,技术勘误把它移除了。glibc 从 2.38 起跟上了,man 的 HISTORY 一节写明,这个版本起它实现的就是 C17 的口径,所以咱们本机 2.44 的实测里:

```text
aligned_alloc(64, 100)    -> 0x5c328280c000 errno=0 (size 非倍数,C17 放宽后 glibc 接受)
```

100 不是 64 的倍数,它照样成功了,errno 也是干净的。您在老 glibc 或别的 libc 上跑,它是有权拒绝的,移植的时候留意 libc 的版本就好。

C++ 这边还有两处自动的通路,咱们也验了。一处是类型自带的 `alignas(64)`,new 它的时候编译器自动改走对齐版本的 operator new 与 operator delete,这是 C++17 修掉的 over-aligned 类型老毛病,实测的地址 %64 等于零。另一处则轮到了 std::pmr,`new_delete_resource()->allocate(1000, 4096)` 交回来的地址同样落在 4096 的倍数上,对齐要求超过默认 new 的 16 时,它就会转发给对齐的 new。所以说,只要您把对齐写进了类型或者接口参数里,链条上的每一环都会自己接上,不需要您一环环手动指定对齐。

## E2 为什么要对齐:一条会段错误的加载

咱们既然要专门指定对齐值,那总得有人真的在乎它。头一位在乎的是 SIMD 指令集。SIMD 是单指令多数据的意思,x86 上跑的这一套,打头阵的是 SSE,一条指令吃的是 128 位,AVX 是 Advanced Vector Extensions 的缩写,后来把宽度翻到了 256 位。咱们在 intrinsic 头文件 `<immintrin.h>` 里打交道的对象就是 `__m256i`,它是一个 256 位、合 32 个字节的整数载体。加载它的函数是一对兄弟,名字上就差一个 u:不带 u 的版本要求地址 32 字节对齐,带了 u 的 unaligned 版本来者不拒。咱们写个探针,分三步走:对齐的地址喂给对齐版,任意的地址喂给 unaligned 版,末了把一个铁定未对齐的地址喂给对齐版。未对齐地址的造法很老实,咱们从一块 32 对齐的内存起步,把地址往右挪了 8 个字节,得到的 %32 恰好是 8。

```text
[1] alignas(32) 静态数组   addr=0x62068e8b5080  %32=0
    _mm256_load_si256 成功(vmovdqa 吃下对齐地址)
[2] 对齐块偏移 8 字节       addr=0x6206c0ed8068  %32=8
    _mm256_loadu_si256 成功(vmovdqu 对任何地址都行)
[3] 未对齐地址 + _mm256_load_si256(vmovdqa)—— 预期崩:
  ==> SIGSEGV 捕获! si_code=128(SI_KERNEL) si_addr=(nil)
```

欸,它真的崩了。可您再细看它崩出来的报告,越看越不对劲了,si_addr 的读数居然是 nil。咱们在 [L02](../file-io/02-mmap-memory-mapping.md) 里见过的段错误,不管是不小心写了只读页,还是摸了未映射的地址,si_code 走的是 SEGV_ACCERR 或 SEGV_MAPERR,si_addr 都老实指着一个真实的出错地址。si_addr 的字节级精度,是[虚拟内存 API 篇](./02-vm-apis.md)的 guard 页实验量过的:您写的是哪个字节,它报的就是哪个字节。这回的读数换了个样:si_code 是 128 的 SI_KERNEL,si_addr 压根就没有了意义。原因在硬件那一层:对齐版加载编译成的是 `vmovdqa`,指令编码里写死了对齐的要求,地址不合格的时候,CPU 报的就是一次 #GP(general protection 通用保护异常),压根没走到缺页的那一步。内核把 #GP 也翻成了 SIGSEGV 递给咱们,但翻译不出一个合理的出错地址,只能交回 SI_KERNEL 加一个无意义的 si_addr。所以您以后在日志里看见 si_code=SI_KERNEL 的段错误,头一个该怀疑的对象就是指令级的违规,对齐是最常见的一种,别按缺页的思路去查页表。

光听笔者转述 vmovdqa 还不算数,汇编咱们得亲眼过目。两个函数体都在存档的 e2_disasm.txt 里,咱们只看头一行:

```text
0000000000001370 <_ZL12load_alignedPKDv4_x>:
    1370:  c5 fd 6f 07    vmovdqa (%rdi),%ymm0

0000000000001330 <_ZL14load_unalignedPKv>:
    1330:  c5 fe 6f 07    vmovdqu (%rdi),%ymm0
```

编码 c5 fd 6f 对 c5 fe 6f 差的只有中间的一个字节,字节内部差的比特是两个,交出来的就是 vmovdqa 对 vmovdqu,a 取的是 aligned 的头字母,u 取的是 unaligned 的头字母。咱们在注释里找不到对齐的要求,在约定里也是找不到的,它就写在指令的编码里,CPU 拿到了就照办。实验里的防守动作也交代一句:装载函数挂了 `target("avx2")` 的属性,不然的话,编译器根本不会发任何的 AVX 指令,函数还消费了全部四个 64 位的通道,不然加载可能被缩窄成 128 位的 XMM 版,对齐要求跟着就降到 16 字节了,证据也就不纯了。

### 同一条缓存行上的两个计数器:7.11 倍

第二个在乎对齐的家伙是缓存行。现代 CPU 的缓存以**缓存行**(cache line)为单位搬数据,x86-64 上一行的体量是 64 字节。两线程的场景里,要是两个各自私有的变量挤在同一行上,缓存一致性协议就得在两个核心之间来回地搬运那一行,程序在语言层面上明明没有任何的共享,性能却掉了好几倍,它就是**伪共享**(false sharing)了。机制在 vol5 的 [CPU 缓存与线程](../../../../vol5-concurrency/ch00-concurrency-fundamentals/03-cpu-cache-and-os-threads.md)里完整讲过了,MESI 与 RFO 咱们不重讲,这里只补上本机的数字。[共享内存篇](./03-shm.md)的环形队列里,head 与 tail 各占一条缓存行的排法,防的也正是它。

实验里咱们开了两个线程,各对自己私有的计数器做 4 亿次的 volatile 自增。near 的变体里,两个 `uint64_t` 相邻地躺在 BSS 中,中间隔了 8 字节,住的是同一行,far 变体用 `alignas(64)` 的结构体,把两个计数器各按在了一行上。三轮交错地跑,咱们取中位数:

```text
中位数:near(同 line)=529.0 ms   far(隔开)=74.4 ms   倍差=7.11x
```

七倍的差距。您的代码没有 data race,两个变量谁也没碰过谁的边界,慢的却全是硬件在搬运。修法您已经看到了:far 变体把 `alignas(64)` 写进了类型,本实验的计数器是静态变量,对齐的落位由链接器负责。动态分配的对象则轮到对齐 new 接手,那是 E1 验过的,C++17 会自动把每个实例放到 64 的倍数上,改动的同样只有一行,收益是实打实的 7 倍。对齐的价值,到这儿就有了两份具体的凭证:一份是 vmovdqa 崩给您看的,另一份是缓存行慢给您看的。

## E3 透明大页:madvise 生效了,大页却是零

对齐的事到这儿告一段落,咱们把目光挪到页的大小上。x86-64 的普通页是 4 KiB,页表还支持着 2 MiB 与 1 GiB 的两档大页。用大页的收益可以用缺页次数直说:咱们同样把 1 GiB 摸满,4 KiB 的页要 262144 次缺页,2 MiB 的页只要 512 次,页表自身的开销也小一个量级。拿大页的路子有两条,一条是显式的 hugetlb,内核里备着的是一个预留池,程序用 MAP_HUGETLB 从池里整页地领,那是 E4 的内容。另一条就是本节的 THP(Transparent Huge Pages,透明大页):内核在幕后自动把符合条件的 4 KiB 页合并成 2 MiB 的大页,对程序来说它是透明的,名字里的 transparent 说的就是这个。

THP 的总开关在 sysfs,`/sys/kernel/mm/transparent_hugepage/enabled` 备了三档:always 的意思是全局自动,never 的意思是全局关死,madvise 的意思是按程序自己的点名来。点名用的正是 madvise,也就是咱们在[虚拟内存 API 篇](./02-vm-apis.md)里摸过的那个接口。咱们一调 `madvise(p, len, MADV_HUGEPAGE)`,就是往一段映射上挂大页的意向。本机的 sysfs 停在 `[madvise]`,按教科书的说法,咱们点了名,内核就该给咱们。E3 的基准程序 e3_thp 就是按这个预期写的:mmap 一块 1 GiB 的匿名内存,给映射挂上 MADV_HUGEPAGE 的意向,每 4096 字节写上了一个字节,把整段摸了个遍,然后咱们读 smaps 里的 AnonHugePages。出厂状态下的输出是这样的:

```text
HUGEPAGE    A 首触写 1 GiB(每页 1 字节):   639.5 ms  AnonHugePages=0 kB
    Size:            1048576 kB
    AnonHugePages:         0 kB
    THPeligible:           0
    VmFlags: rd wr mr mw me ac sd hg
  ==> 提示了 HUGEPAGE,大页却是 0:进程级开关压过了 sysfs(详见 e3_thp_probe)
```

欸?咱们没写错任何一行,madvise 的返回值也是零,VmFlags 里都亮起 `hg` 了,这个标志说明咱们的点名已经记进了这段 VMA 的旗标里。可内核给出来的,AnonHugePages 给的是 0,THPeligible 给的也是 0,连一个 2 MiB 的页都没有。hg 与零的同时出现,这就是本篇最值钱的一个现场。

咱们没有猜,存档里的诊断探针 e3_thp_probe 就是干这个的,它跟计时的基准程序 e3_thp 分了工,咱们拿着探针一步一步地排除。第一步查 sysfs:全局选的是 `[madvise]`,2048kB 档选了 `[inherit]` 跟着全局走,shmem 的设置是 never,全都是正常的。第二步咱们查 madvise 到没到内核:hg 在 VmFlags 里,到了。第三步查内核试没试:/proc/vmstat 的 thp_fault_alloc 与 thp_fault_fallback 计数,探针起跑时打的基线是零增量,出厂的那一轮跑完了之后,程序里的差值段要等到清开关之后才有。零尝试的说法,咱们靠后面的对照反推:清掉了开关再重跑同样的操作,计数恰好只加那一轮自己的份额,出厂的那一轮一次都没有贡献过。接着咱们再补一刀,咱们往同一片区域发 MADV_COLLAPSE,这是 6.1 起内核提供的同步合并接口,能把已驻留的 4 KiB 页捏成 2 MiB,返回的是 EINVAL。诊断到了这儿,嫌疑就只剩下进程自己了。

## THP 的第三层开关:WSL2 把它按在了进程身上

探针的最后一行就是答案,咱们一起看:

```text
prctl(PR_GET_THP_DISABLE) = 1   <-- WSL2 的 init 给整个进程树设了它
```

`PR_GET_THP_DISABLE` 读的是进程身上一个叫 MMF_DISABLE_THP 的标志位,设上了之后,这个进程就跟 THP 无缘了。man 2 PR_SET_THP_DISABLE 对它的描述相当强硬:设了它之后,进程的 THP 就被完全禁用了,而且全局开关与 MADV_COLLAPSE 都救不回来。这个标志从 Linux 3.15 就有了,本意是给改不了源码的作业程序留一个手动关闭的把手。而咱们身上的标志,是继承来的:它跟着 fork 与 exec 一路传了下来。WSL2 里 systemd 之前有微软自己的 init 链,它给整个进程树都设了它,咱们的实验程序一出生就带着。sysfs 里写着的是 `[madvise]`,那说的是内核全局的口径,进程身上的这一层开关,它是既看不见也管不着的,hg 的旗标照记不误,大页的分配动作却照免。教科书讲 THP 给了两层,sysfs 占的是一层,madvise 占的是一层,WSL2 在下面垫了第三层。

那咱们还有没有救?man 页说了,prctl(PR_SET_THP_DISABLE) 的参数传零,就是清除标志的意思。咱们试了,本机的 6.18 内核允许无特权进程清掉自己身上的它。清完了之后,咱们把同一份探针再跑一遍:

```text
② prctl(PR_SET_THP_DISABLE, 0) 后:PR_GET=0
    mmap+MADV_HUGEPAGE+首触(0x7a126c800000):
    [复活] AnonHugePages:     32768 kB
    [复活] THPeligible:           1
---- ② 之后(相对本进程启动的增量) ----
    thp_fault_alloc                +16
```

32 MiB 的区域,AnonHugePages 跳到了 32768 kB,thp_fault_alloc 计数加了 16,正好凑成了 16 个 2 MiB。MADV_COLLAPSE 也活过来了:一段不带任何 madvise、纯 4 KiB 首触的区域,咱们一发 COLLAPSE,rc 归了零,AnonHugePages 从 0 涨到了 32768 kB,thp_collapse_alloc 计数也加了 16。正反的对照一次拿全,根因也就查实了。您要是在裸机 Linux 上跑,没有 init 给您设标志,THP 就只听 sysfs 与 madvise 的两层。您反过来在 WSL2 里跑 THP 实验,拿到一串零的时候,就查 prctl 的读数,别急着怀疑自己的代码。咱们还得留一句余地:清标志的这一手,靠的是本内核的允许,微软哪天收紧了,咱们也拦不住,以您机器的实测为准。

开关清了,大页真的落到 512 个 2 MiB 上,性能差多少就该见分晓了。清开关之后的性能对照,咱们还是在基准程序 e3_thp 里做的,首触的还是那 1 GiB,前前后后跑了三轮,中位数取的是中间的那一轮:

```text
中位数(3 轮):
  A 首触  HUGEPAGE=55.4 ms  NOHUGEPAGE=300.5 ms  倍差=5.43x
  B memset HUGEPAGE=39.7 ms  NOHUGEPAGE=40.7 ms  倍差=1.02x
```

首触的那一轮拿到了 5.43 倍,缺页从 262144 次掉到了 512 次,smaps 里的 AnonHugePages 满格 1048576 kB,1 GiB 整段都大页化了,这样的收益来得干净利落。可 memset 的那轮只有 1.02 倍,基本就是在噪声里打了平。两行的数字咱们都摆在上面了,原因也得说清:memset 走的是纯流写,数据早都驻留了,咱们每写一个字节,都得让页表翻译一次地址,翻译的结果就缓存在 TLB 里,4 KiB 页的 TLB 条目确实比 2 MiB 页费,可流写的带宽大头在内存本身,TLB 那点优势摊进去就看不见了。大页的收益集中在缺页次数与翻译缓存的密度上,指望它给已经暖热的流写提速,是不现实的。网上偶有把 THP 当万灵药的口径,这两行数字就是现成的反例。

## E4 显式大页:空着的预留池

另一条路就是显式大页了,它的机制是彻底不透明的:管理员提前在内核里预留一个 2 MiB 大页的池子,程序 mmap 的时候带上 MAP_HUGETLB,从池里整页地领,领多大的页,咱们可以用 MAP_HUGE_2MB 或 MAP_HUGE_1GB 指明,不指明的话,就取默认的页大小,/proc/meminfo 的 Hugepagesize 字段,会告诉您默认值是多少。咱们本机的池子状态,E4 的程序一跑起来就把它读了:

```text
nr_hugepages          = 0
    HugePages_Total:       0
    Hugepagesize:       2048 kB
```

池子是空的。咱们还是把三档全试了一遍,外加一个不带 HUGETLB 的对照组:

```text
MAP_HUGETLB 尝试(池为空,预期 ENOMEM):
MAP_HUGETLB|MAP_HUGE_2MB, 2 MiB        -> MAP_FAILED  errno=12 (Cannot allocate memory)
MAP_HUGETLB|MAP_HUGE_1GB, 1 GiB        -> MAP_FAILED  errno=12 (Cannot allocate memory)
MAP_HUGETLB(默认页大小), 2 MiB    -> MAP_FAILED  errno=12 (Cannot allocate memory)

对照组(不带 HUGETLB,普通匿名映射):
普通 MAP_ANONYMOUS, 2 MiB            -> 0x74f0fc800000  成功
```

三档交回来的全是 ENOMEM。这里有个容易误会的点:man 2 mmap 的 ERRORS 里,MAP_HUGETLB 确实配了一个 EPERM,条件是调用者没有 CAP_IPC_LOCK 的能力,可咱们实测的三档,交回来的全是 ENOMEM。按内核源码的口径,EPERM 的那道检查,挂在 SysV 共享内存(SHM_HUGETLB)一类的路径上,咱们走的匿名 MAP_HUGETLB 压根不经过它。实测能支撑的说法只有一句:池子是空的,预留落了空,失败码也就停在了 ENOMEM 上。您想把池子填上,路全在 root 手里:可以写 hugepages=N 的启动参数,可以往 /proc/sys/vm/nr_hugepages 里写入预留的页数,再或者就是挂一个 hugetlbfs 的文件系统了。咱们也试了写,open 就被挡回来了:

```text
open(nr_hugepages, O_WRONLY) -> 失败 errno=13 (Permission denied)  [无 root,如实记录]
```

所以本机无特权环境下的口径,咱们得说在明处:显式大页的路子走不通,能走的大页只有 E3 的 THP。显式大页与 THP 的取舍也就顺理成章了:THP 不占预留的内存,按需地合并,代价是 khugepaged(内核里负责把普通页并成大页的后台线程)在幕后干活,何时出手不由您定,有它自己的脾气,显式大页则是一页管一页的,延迟也是稳定的,只是开池子的活儿归管理员,数据库一类的常驻大户用的是它,E4 的 ENOMEM 就是池子没预留的直接证据。

## E5 Overcommit:承诺与占用是两回事

大页与预留池的话题,把咱们引到了虚拟内存最后一个根本的问题上:咱们 malloc 一份 512 GiB,凭什么成功?或者凭什么失败?Linux 对内存承诺的口径叫 **overcommit**(超额承诺):内核允许把总量超过物理内存加 swap 的虚拟内存分配出去,赌的就是程序拿了地址未必真用。口径的决定权在 `/proc/sys/vm/overcommit_memory` 手上,man 5 proc_sys_vm 里写了三档:0 的档位是启发式,它也是默认的档位,只拦那些明显离谱的单次申请,1 的档位是永远放行,2 的档位是严格档,全系统的承诺量不得超过 CommitLimit,按 `(RAM - 大页预留) × overcommit_ratio / 100 + swap` 的公式算。本机的档位是 0,CommitLimit 的读数约 42.5 GiB,那是模式 2 的尺子,模式 0 是不用它的。

E5 咱们从一次阶梯下探开始,malloc 的量从 512 GiB 一路减半:

```text
  malloc(512 GiB) -> (nil) errno=12 (Cannot allocate memory)
  malloc(256 GiB) -> (nil) errno=12(被拒)
  malloc(128 GiB) -> (nil) errno=12(被拒)
  malloc( 64 GiB) -> 0x733e09fff010 errno=0(成功)
```

512、256、128 全被拒了,64 GiB 却成功了。拒绝线落在哪儿?本机 MemTotal 55522928 kB 加 SwapTotal 16777216 kB,合计的量约 69 GiB。128 压在它的上面,被拒了,64 呆在它的下面,放行了,启发式的尺子就是 RAM 加 swap 的量级,单次的申请一旦压过了它,拿到的就是 NULL。man 页对模式 0 只给了 heuristic 一个词,没有写死的公式,69 GiB 是本机的实测口径,您机器上把这组实验一跑,自己的线在哪儿也就清楚了。

同一份的 512 GiB,换个要法就完全是另一个结局了。mmap 带上了 MAP_NORESERVE,明示内核不做承诺的预留,它立刻就成功了。成功之后咱们一段一段读 /proc/self/status,数字链是这样的。输出里的阶段号从 0 直接跳到了 3,不是咱们节选掉了中间的输出,程序里的标号本来就只有四个,中间缺的 1 号和 2 号从未存在过:

```text
  [阶段0 基线]
    VmSize:    7304 kB      VmRSS:    4036 kB
  mmap(512 GiB, MAP_NORESERVE) -> 成功 errno=0
  [阶段3 阶段映射后,一字节没碰:VmSize +512 GiB,RSS 不动]
    VmSize: 536878216 kB    VmRSS:    4044 kB
  [阶段4 只写 1 GiB:VmRSS 涨到 ~1 GiB 量级,承诺的其余 511 GiB 不占物理页]
    VmSize: 536878216 kB    VmRSS: 1052620 kB
  [阶段5 munmap 后:回到基线]
    VmSize:    7304 kB      VmRSS:    4044 kB
```

VmSize 涨了 512 GiB,VmRSS 是纹丝不动的,咱们写了 1 GiB,它就只涨了 1 GiB,munmap 之后双双回了基线。虚拟的承诺与物理的占用是分开记的。这样的便利是有代价的,咱们到 E6 说。

> 在数字讲到一半的时候,笔者得插一段自己栽过的跟头。第一版的阶梯程序里,malloc 回来只判了一下空就 free,打印的也只有成败。跑出来 256 GiB 是成功的,比现在这版的记录乐观得多。咱们用 strace 一跟,真相很难堪:那几次 malloc 的后面,根本没有对应的系统调用,咱们压根就没向内核要过内存。`gcc -O2` 判定这对 malloc/free 只做空判、毫无可观察的效果,就把它们整对优化掉了,程序打印的成功,是编译器替咱们脑补的。修正的办法是把指针本身用 %p 打出来,编译器折不掉指针的值,再用 strace 复核每个档位是不是真的发了 mmap。您以后做分配类的测量实验,请您把防备心随身带着,优化器不只优化您的程序,它还会不动声色地优化掉您的实验。

## E6 OOM Killer:安全余量里的一次逼近

承诺出去了,物理内存却是有限的。当物理内存与 swap 真的见底,内核也没有页可以回收的时候,它最后的手段就是 **OOM Killer**(Out Of Memory Killer,内存耗尽时的处决机制):内核给每个进程打一个分,挑分数最高的杀掉,把内存抢了回来。分数所在的文件就是 `/proc/pid/oom_score`,取值的范围是 0 到 1000,分数越大的进程,被杀的次序就越靠前。E6 咱们不真触发它,WSL2 要是被打挂了,后面几章的实验也就别想跑了,咱们做的是只读观察,外加一次控制在安全余量里的占用与回收。

只读的部分一开场,就给咱们送了一个意外:

```text
==== ① 只读观察 ====
  /proc/self/oom_score     = 666 (0-1000,越大越先被杀)
  /proc/self/oom_score_adj = 0 (基线 0)
```

咱们的小进程,占内存的量级才几兆字节,分数却挂在了 666。笔者把机器上别的进程也读了一遍,小进程几乎清一色的 666。可见这个分跟您想象中的绝对内存量对不上,它走的是相对的口径,进程之间的排序才有意义,绝对值本身倒是不说明问题的。分数旁边有个可写的旋钮 oom_score_adj,取值的范围是正负 1000,效果是加到分数上去的。咱们实测了它的可写性:

```text
write(oom_score_adj, "250") -> 3 成功
write(oom_score_adj, "-1000") -> -1 失败
  负值写入被拒(errno=13 Permission denied)
```

咱们自己写正值,加几个字节就成功了,咱们想把自己调得更该杀,内核是最大方的。写负值的时候,把自己往免死的方向挪,要的是 CAP_SYS_RESOURCE 的能力,无特权的咱们直接收了 errno 13 的 EACCES。您细品一下这个不对称:您愿意自损,随您,咱们想自保,就得拿到特权再说了。

真正碰内存的部分,咱们的刻度定在 MemAvailable 的 60%:程序起来的时候读一遍 MemAvailable,按它的六成 calloc,再按每 4096 字节一个的节奏实写,让内核真的交出物理页,然后咱们再 free。三段的读数如下:

```text
  [touch 前基线]
    VmRSS:  4208 kB      MemAvailable=46558532 kB   oom_score=666
  [touch 后:RSS 涨到目标量级,MemAvailable 同步下降]
    VmRSS: 27939332 kB   MemAvailable=19415616 kB   oom_score=924
  [free 后:RSS 落回,MemAvailable 回升 —— 占用/回收链条闭合]
    VmRSS:  4212 kB      MemAvailable=46788072 kB   oom_score=666
```

VmRSS 涨到约 26.6 GiB 又落了回去,MemAvailable 也同步地开合,oom_score 跟着 666、924、666 走了一圈,占用与回收的链条闭合了,而且全程离内存耗尽远得很,余量一直稳稳的。dmesg 咱们也做了全量的扫描,694 行的输出里,oom、out of memory、killed process 的匹配是零条,没有杀手出面的记录,实验干净地结束了。这组读数把 E5 留下的另一半验证补齐了:calloc 的成功只是承诺,touch 才真的占了物理页,free 之后大块走 mmap 的内存立即归还,咱们在[布局篇](./01-memory-layout.md)看过的 malloc 分水岭,正好管着 free 这一头的归还路。那 overcommit 的代价到底是什么?就是承诺与占用的缺口没人担保:512 GiB 的映射要是真被咱们一页页写满,写到了底线之后,新页就要不到了,OOM Killer 就得出来收拾局面了,而它挑中的,未必是您觉得最该死的那个进程。

## 另一侧怎么看

Windows 的那边,大页的路子跟 Linux 的显式大页是一路的脾气,也不走透明合并的路子。VirtualAlloc 与 CreateFileMappingW 认的也都是 MEM_LARGE_PAGES 与 SEC_LARGE_PAGES,粒度的多少由 `GetLargePageMinimum()` 给出,可它要的是 SeLockMemoryPrivilege 特权。[文件映射篇](../../windows/file-io/02-file-mapping.md)当年就探过这个门槛:本机的用户没有被授予这个特权,`VirtualAlloc(MEM_LARGE_PAGES)` 与 SEC_LARGE_PAGES 双双收下了 1314,也就是 ERROR_PRIVILEGE_NOT_HELD 的错误码,普通页的对照分配则照常成功。您对一下 E4 的 ENOMEM 与这里的 1314,两边挡您的层次不一样:一个挡在了池子空,一个挡在了没资格,而两边的结果是一样的:都得管理员把配置提前做好,您才用得上大页。承诺与占用的分野,Windows 侧也有自己的版本,VirtualAlloc 的 MEM_RESERVE 与 MEM_COMMIT 两段式,预约与提交分开地走,[文件映射篇](../../windows/file-io/02-file-mapping.md)的 SEC_RESERVE 实验,当时就对照着 Linux 的 overcommit 讲过一轮了。Windows 侧内存管理的正式篇章已经在了,头一篇讲的就是 VirtualAlloc,展开的细节咱们到[虚拟内存篇](../../windows/memory/01-virtualalloc.md)再讲。

Linux 侧的内存管理,到 OOM Killer 这里就走完了。咱们回头一看,从一张 maps 的全图起步,认过每一段映射的出身,量过 malloc 的分水岭,摸过 mprotect 与 madvise 的深浅,跨过进程间的共享,最后落在对齐、大页与承诺这三件收尾的事上。收尾的正是 OOM Killer:它挑出来杀掉的,是一个个具体的进程。一个进程怎么从 fork 里出生,变身的时候换掉的是程序映像、fd 表默认是跟随的,僵尸怎么收,信号怎么递到它手上,这些就都交给下一章了,咱们到时候接着看。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="aligned_alloc(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/aligned_alloc.3.html"
  />
  <ReferenceItem
    :id="2"
    title="posix_memalign(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/posix_memalign.3.html"
  />
  <ReferenceItem
    :id="3"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="4"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="5"
    title="PR_SET_THP_DISABLE(2const)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/PR_SET_THP_DISABLE.2const.html"
  />
  <ReferenceItem
    :id="6"
    title="proc_sys_vm(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_sys_vm.5.html"
  />
  <ReferenceItem
    :id="7"
    title="proc(5) — oom_score / oom_score_adj"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc.5.html"
  />
  <ReferenceItem
    :id="8"
    title="Transparent Huge Support(内核文档)"
    publisher="Linux kernel documentation"
    url="https://docs.kernel.org/admin-guide/mm/transhuge.html"
  />
  <ReferenceItem
    :id="9"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
