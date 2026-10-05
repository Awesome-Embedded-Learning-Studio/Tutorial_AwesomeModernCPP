---
title: "共享内存:shm_open 与映射"
description: 两个没有亲缘的进程要共享同一片内存,靠的是 shm_open 在 /dev/shm 里立起一个命名对象,同一物理页映射进两个地址空间。本篇实测命名对象的完整生命周期(新对象尺寸为 0 要 ftruncate、未 unlink 二次创建吃 EEXIST 而 unlink 后同名重建畅通、旧映射读 0x1111 新实体写 0x2222 互不可见、umask 截权限、root 的 0600 挡住 user 而 CAP_DAC_OVERRIDE 反向放行),用三轮丢 9.62%~23.70% 的计数竞态加 addq 无 lock 前缀的汇编坐实看得见不等于用得对,PTHREAD_PROCESS_SHARED 互斥 44.7 ms 与进程间信号量 58.1 ms 对照修复,招牌实验是 shm 里的 SPSC 无锁环形队列:一百万条零丢失零乱序,sched_yield 背压反而比 CPU 自旋快六成以上,归因 cache line 乒乓,钉核对照排除同核调度。1 MiB 搬运 7304 对 2349 MiB/s 约三倍于 pipe,memfd_create 不留名字 close 即消失,结尾预告 Windows 页面文件后备的命名映射
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 19
prerequisites:
  - "mmap 内存映射:把文件贴进地址空间"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "内存序详解"
  - "无锁编程基础"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
  - atomic
  - 内存管理
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 共享内存:shm_open 与映射

内存管理的前两篇,咱们一直在打点自己进程的地址空间:布局篇领着咱们把 text、heap、stack 在 `/proc/self/maps` 里认了门,虚拟内存 API 篇把 mprotect、madvise 都练了一遍手。不过手艺再熟,咱们在自己的地址空间之外,也从没立过一片两个进程共写的内存(上一篇结尾的 process_vm_readv,也只算隔着系统调用的一眼侦察)。这一篇咱们要跨出去了:两个连亲缘都没有的进程,咱们让它们共享同一片物理内存。落到机制的层面,内核把同一批物理页同时映射进两个进程的地址空间,A 进程写的字节,B 进程拿指针就读到了,中间既没有内核的搬运,也不经过任何的系统调用。

相认的问题,咱们在 [mmap 内存映射](../file-io/02-mmap-memory-mapping.md) 那篇里见过一半(按系列的简称约定,咱们下文叫它 L02):MAP_SHARED 的写直接落在页缓存,fork 出的子进程自己 open、自己 mmap,写下的那个 X,父亲在自家的映射里立刻就读到了。不过那一对是父子进程,共享的媒介是一个真实存在的文件。要是 server 和 client 是各自独立启动的进程,彼此连对方的名字都不知道,凭什么指望它们找到同一片内存?咱们给的答案,就是给内存起名字:shm_open 在一个全体进程都看得见的地方,登记一个能按名字打开的对象。有名字的共享内存对象,咱们后文就叫它命名对象。

名字解决了相认,真正的麻烦才刚刚开始。咱们让两个进程对同一片内存又读又写,次序就没有任何人管了:内核负责的只是把页映射进来,它不知道您写的整数是一个队列的头部,更不知道您哪个字节该落在后面。共享内存的快,快的正是没有内核在中间守着,而它的代价,就是同步的活全部落回咱们自己手上。本篇咱们把链走全:命名对象的一生、竞态的实测、两条共享途径、一把不带锁的环形队列,再跟 pipe 拼一场吞吐的对照,末一站是不留名字的 memfd_create。

实验环境咱们交代清楚,后面的数字都要靠它对表。全部实验出自笔者的 WSL2:内核是 6.18.33.2-microsoft-standard-WSL2 的同一构建,CPU 用的是 AMD Ryzen 7 9700X,WSL2 的视角下有 16 个逻辑核,编译器用的是 g++ 16.2.1,咱们统一按 `g++ -std=c++20 -Wall -Wextra -O2` 编译,竞态与队列的实验另加 `-pthread`,glibc 用的是 2.44。代码与全部的原始输出,都收在仓库的 `code/volumn_codes/vol8/systems-programming/linux/memory/03-shm/` 目录,README 里还带着每条复跑的命令,正文里贴的输出块是节选,都以存档的原文为准。实验的编号按 E1 到 E6 排,跟着存档的目录走。思维基石两篇([RAII 篇](../../thinking/01-raii-paradigm.md) 与[错误处理篇](../../thinking/02-error-paradigm.md))定义的 `unique_fd`、`mapped_region`、`sys_call`,这一篇同样也是用不上的,咱们的实验贴着系统调用的原貌走,用的全是裸调用。

## 命名对象:给内存起一个名字

shm_open 的原型比咱们想象中小巧,要的头文件是 `<sys/mman.h>`,oflag 的取值来自 `<fcntl.h>`,mode 的类型则来自 `<sys/stat.h>`。man 3 shm_open 对名字有一条可移植的写法:开头是一个斜杠,后面跟着的字符里不许再有别的斜杠,整个名字串的长度连斜杠在内,不超过 NAME_MAX(255)个字符的上限。它交回来的是一个 fd,而不是指针,open 的那套手艺拿过来就能用,这正是 POSIX 这套接口讨喜的地方。做同一件事的还有老前辈 System V,shmget 那一套 id 加 key 的体系自成一派,咱们不掺和,本篇只走 POSIX 的文件路数。

这个 fd 的背后是什么?man 页的说法是,Linux 把共享内存对象放进一个专用的 tmpfs 文件系统,通常就挂在 `/dev/shm` 的名下。tmpfs 是内容全待在内存里的文件系统,L02 的 Dirty 实验里咱们就交代过 `/tmp` 是它的地界。既然它是躺在文件系统里的实体,咱们就能用 `ls` 看见它,E1 的起手式就是冲着这个去的:

```cpp
// e1_lifecycle.cpp(节选):shell() 负责打印并执行 ls -l /dev/shm,fstat 读尺寸
int fd = shm_open("/lm03_demo", O_RDWR | O_CREAT | O_EXCL, 0600);  // 建立命名对象
struct stat st {};
fstat(fd, &st);         // size=0:新对象的尺寸是 0
ftruncate(fd, 65536);   // 定尺寸,尺寸 0 的对象直接 mmap 会吃 EINVAL
void* p = mmap(nullptr, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
auto* gen1 = static_cast<unsigned long long*>(p);
gen1[0] = 0x1111ULL;    // 写入第一代标记
```

```text
$ ./e1_lifecycle
$ ls -l /dev/shm
total 0
[1] shm_open("/lm03_demo", O_CREAT|O_EXCL|O_RDWR, 0600) 成功,fd=3
    刚创建时 fstat:size=0 mode=0600(新对象尺寸为 0,不 ftruncate 直接 mmap 会 EINVAL/长度 0)
[2] ftruncate(fd, 65536) 后:size=65536
[3] mmap MAP_SHARED 完成,写入第一代标记 0x1111 + 文本 "generation-1"
$ ls -l /dev/shm
total 4
-rw------- 1 charliechen charliechen 65536 Oct  3 10:49 lm03_demo
```

咱们把输出慢慢看。头一段 `ls` 的时候,`/dev/shm` 是空空如也的。等 shm_open 建好对象、ftruncate 定了尺寸,咱们再列一次,`lm03_demo` 就躺在那儿了:从属主、权限位到字节数,它跟普通文件长得一个样了。名字空间指的就是 `/dev/shm` 里的目录项,咱们照字面理解这话就行。尺寸为 0 是新对象的出厂状态,man 页也写明了,对象的尺寸可以用 ftruncate(2) 来定。咱们要是不 truncate 就去 mmap,长度 0 的映射按 man 2 mmap 的条文回 EINVAL,实验里咱们没吃这一下,是 e1 存档输出里写明白的边界,您动手的时候能省一趟弯路。

编译链接上的一个新旧差异,咱们交代在前面:man 3 shm_open 的页面到今天还写着链 real-time 库(`-lrt`),那是一份老黄历了,glibc 2.34 起的链接已经不需要它。要照顾 2.34 之前的老系统,咱们把 `-lrt` 留着也无妨,新系统上它是无害的。

> 老黄历的来历咱们顺手交代:librt 的符号在 glibc 2.34 并进了 libc,笔者的 glibc 2.44 上,链接命令不带 `-lrt` 就直接通过了,咱们拿 `nm` 再看一眼 `librt.so.1`,里面已经找不到 shm_open 了。

还有一处权限上的细节:shm_open 的 mode 参数和 open 一样,要过进程的 umask。umask 是权限的掩码,新建对象的时候,它会把 mode 里对应的位摘掉。E1 的附实验里,咱们两次都请求 0666:umask 是 0022 时,实际生效的权限是 0644,umask 归零之后才拿到完整的 0666。想给别人开读写位的对象,umask 的这层影响要算进预期,必要时咱们拿 `umask(0)` 顶一下,再把原来的值还回去。

## shm_unlink:名字没了,实体还在

对象有了名字,新的问题跟着名字来:两个进程抢同一个名字怎么办?名字还在的时候做二次创建,当裁判的就是 O_CREAT 配 O_EXCL。E1 的第 4 步实测,咱们在未 unlink 的时候,把同名的创建再发一次 `shm_open("/lm03_demo", O_RDWR|O_CREAT|O_EXCL, 0600)`,返回的是 -1,errno 给的是 17(EEXIST、文件已存在),和 [POSIX 文件 I/O](../file-io/01-posix-file-io.md)(L01)里 open 的 O_EXCL,脾气是一个样的。反过来的第 5 步,咱们不带 O_CREAT 纯按名字打开,成功了,第二个映射读到的就是 0x1111,同一实体的身份跑不了。

真正的好戏,咱们把镜头挪到 unlink 上:`shm_unlink("/lm03_demo")` 干的是删名字这件事,有意思的地方,全在删除之后的这一段:

```text
[6] shm_unlink("/lm03_demo") 返回 0,名字没了
  readlink(/proc/self/fd/3) -> /dev/shm/lm03_demo (deleted)
$ ls -l /dev/shm
total 0
[7] unlink 后旧映射:仍读到 0x1111 "generation-1"
    继续写 gen1[1]=42 也成功——unlink 只删名字,实体活到所有映射/fd 关闭
[8] unlink 后同名 O_CREAT|O_EXCL 重建成功(fd=4),写入 0x2222 "generation-2"
    同一时刻旧映射看到的还是 0x1111 "generation-1"——同名的两个对象互不相干,证明 unlink 后重建的是新实体
```

咱们一行行对过去。咱们拿 readlink 看 fd 3,链到的路径挂着 `(deleted)` 后缀,和 L02 里删文件之后 maps 行挂 `(deleted)` 是同一套表现:名字从目录里摘掉了,fd 和映射还攥着实体的引用。看第 7 步的结果,旧映射里读到的还是 0x1111,咱们再往 gen1[1] 写个 42,它也照样成功了。第 8 步是全场的主角:名字空出来了,同名 O_CREAT|O_EXCL 的重建,居然一次就成功了。咱们往新实体写 0x2222 的第二代标记,咱们再回头看旧映射,看到的还是 0x1111。

同名的两个对象同时活着,而互相看不见对方。咱们把证据链串起来:unlink 删的是名字,而删不掉实体,实体的寿命由所有 fd 和映射决定,等最后一份引用关掉了,它才真正地消失。名字只是目录里的挂号,谁按了名字来,谁就得到当时挂在名字上的那个实体,名字空出来了再注册,注册的就是新实体。man 3 shm_unlink 的原文也是这个口径:unlink 之后不带 O_CREAT 再开同名会失败,带上 O_CREAT 建出来的,是一个全新的对象。有一处容易记反的地方咱们点一句:EEXIST 出现在没 unlink 就二次创建的场合,unlink 之后重建是畅通无阻的,您别把两个方向弄颠倒。

权限位在这套机制里是不是真管用?咱们让两个 uid 真刀真枪对一遍(E1 的附实验,咱们记在 e1_perm.cpp 里)。跨 uid 的会话借的是 WSL2 的 interop 通道:`wsl.exe -u root --` 进去的就是 uid 0 的 shell,`/dev/shm` 是整个发行版共享的,root 和 user 两个会话看到的就是同一批实体:

```text
[1] root 会话建两个对象:0600 与 0666(umask 0,mode 原样生效)
$ ls -l /dev/shm
-rw------- 1 root root 4096 Oct  3 10:50 lm03_perm600
-rw-rw-rw- 1 root root 4096 Oct  3 10:50 lm03_perm666

[2] user 进程(uid=1000)分别去开:
open /lm03_perm600:失败 uid=1000 errno=13 (Permission denied)
open /lm03_perm666:成功 uid=1000 读到"written-by-uid-0"

[3] 反向:user 建一个 0600 对象,root 去开
open /lm03_user600:成功 uid=0 读到"written-by-uid-1000"
```

root 建的 0600 对象,咱们让 user 进程去开,errno 给的是 13(EACCES),权限位是真的拦住了。0666 的对象,user 打开就成功了,还读到了 root 写进去的内容。反方向的结果最有意思:user 建的 0600,root 还是照样打开了。这可不是权限的失效,放行的是 root 手里的 CAP_DAC_OVERRIDE,它是 Linux 能力位体系里越过常规权限检查的那一枚,特权进程的特殊待遇,咱们照实记下。跨 uid 共享的做法也就清楚了:创建方把 mode 给足、umask 顶掉,或者干脆让低权限的一方当创建者,高权限的一方按名字来开。

## 共享之后,加法就不对了

对象有了,两个进程也相认了,咱们马上请出共享内存最出名的事故:计数器丢更新。E2 的设计是这样的:共享内存的一片区域,头部放着同步的原语,中间躺着一个 `unsigned long long` 的计数器,咱们让两个子进程各加一百万次,期望的终值是二百万。底层用的是匿名 `MAP_SHARED|MAP_ANONYMOUS` 配 fork,两条途径的对照下一节专门做,咱们看的竞态本身,和用哪条途径是没有关系的:

```cpp
// e2_race.cpp(节选):共享头与三种加一姿势,起跑线 start 统一放行
struct Shared {
    pthread_mutex_t mtx;                  // mutex 版用
    sem_t sem;                           // sem 版用
    volatile unsigned long long counter; // volatile 只挡编译器,不提供原子性
    std::atomic<int> ready;              // 子进程就绪计数
    std::atomic<int> start;              // 起跑线,计时口径统一
};
// 两个子进程各跑 n 次,循环体按模式三选一:
//   mutex : pthread_mutex_lock / counter = counter + 1 / pthread_mutex_unlock
//   sem   : sem_wait / counter = counter + 1 / sem_post
//   nosync: 裸的 counter = counter + 1
```

```text
== E2 跨进程计数竞态:三版各 3 轮(每人 1,000,000 次,期望 2,000,000)==

--- nosync ---
nosync  每人 1000000 次:结果 1526047 / 期望 2000000(丢失 473953 次,23.70%) 耗时 1.9 ms(104 万次/秒)
nosync  每人 1000000 次:结果 1628983 / 期望 2000000(丢失 371017 次,18.55%) 耗时 1.5 ms(130 万次/秒)
nosync  每人 1000000 次:结果 1807586 / 期望 2000000(丢失 192414 次,9.62%) 耗时 1.3 ms(158 万次/秒)

--- mutex ---
mutex   每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 60.4 ms(3 万次/秒)
mutex   每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 44.7 ms(4 万次/秒)
mutex   每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 39.8 ms(5 万次/秒)

--- sem ---
sem     每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 57.5 ms(3 万次/秒)
sem     每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 60.7 ms(3 万次/秒)
sem     每人 1000000 次:结果 2000000 / 期望 2000000(无丢失) 耗时 58.1 ms(3 万次/秒)
```

咱们把三轮成绩摆开:1526047、1628983、1807586,丢掉的更新在 9.62% 到 23.70% 之间晃。三轮给了三个数,连丢的量都不稳定,这正是竞态的本来面目:丢多少,看的是两个进程的步伐在运行时怎么交错,每一轮的答案都不一样。

更新是怎么丢的?在机器码的层面,`counter = counter + 1` 是三步的活:把内存的值读进寄存器、寄存器加一、再把结果写回内存。两个进程各跑各的三步,只要 A 读走了旧值、还没写回的时候,B 也读走了同一个旧值,两次加法就并成了一次。空口说是没有用的,咱们把 `-O2` 的汇编请出来,E2 输出的末尾就带着:

```text
== 附:nosync 计数循环的汇编(确认每轮都是回内存的读改写,不是寄存器累加)==
    1523:	f0 83 43 50 01       	lock addl $0x1,0x50(%rbx)
    153b:	48 83 43 48 01       	addq   $0x1,0x48(%rbx)
    155a:	48 83 43 48 01       	addq   $0x1,0x48(%rbx)
    1580:	48 83 43 48 01       	addq   $0x1,0x48(%rbx)
    ↑ 计数是 addq $0x1,内存 的非原子读改写(无 lock 前缀);对比 ready.fetch_add 编出的是 lock addl
```

咱们看 0x153b 往下的三行,计数全被编成了 `addq $0x1,0x48(%rbx)`,是对着内存直接加的。没有 lock 前缀的它,就是一趟普通的读改写:两个核同时执行,彼此也是不打招呼的。头一行的 `lock addl` 是对照组:同一段代码里 ready 的 fetch_add 编出来的就是它,这是 x86 原子指令的标志,挂上 lock 前缀的指令,这趟读改写就不可分割了。一样的加一,差的就是一个前缀,原子性却是天差地别的。

这里咱们得把一个流传很广的误解按住:volatile 挡不了这场事故。它作用的对象是编译器,逼着每次都真的读内存、真的写,别把 load 和 add 提到循环的外面攒一批。E2 的 counter 是带着 volatile 的,少了它的话,`-O2` 之下单进程的视角里,counter 的循环能被优化成读一次、加 N、存一次,丢更新的戏就演不成了。可 volatile 到了核间同步的层面就不管事了,读和写还是两条各自独立的动作,交错是照旧的,更新也是照丢的。咱们真想要原子,下面两段的两把锁就是答案,另一条路就是带 lock 前缀的原子指令。

头一种修法用的是互斥锁。咱们把 `pthread_mutex_t` 放进共享内存,两个进程 lock/unlock 的就是同一把锁。要害在 `pthread_mutexattr_setpshared`:互斥锁默认的属性是 PTHREAD_PROCESS_PRIVATE,只认本进程的线程,锁的本体要跨进程共用,咱们必须把属性设成 PTHREAD_PROCESS_SHARED。man 3 pthread_mutexattr_setpshared 的说法是,设了它,凡能访问这块内存的线程都能操作这把锁,包括别的进程里的线程。锁能被访问的前提咱们也交代了:锁本身就得住在共享的内存里,放在哪个进程自己的堆上都是白搭。E2 的锁就住在 Shared 结构的头部,结果是三轮全中了 2000000、丢的更新一次也没有了,代价是中位的 44.7 ms,比裸加的一两毫秒贵了约三十倍。

第二种修法走的是进程间信号量。`sem_init(&sem, 1, 1)` 的第二个参数是 pshared,给 1 就是进程间的共享,和互斥锁的 PROCESS_SHARED 是同一个意思换了一种原语。信号量是带计数的同步原语,它的 wait 见到大于零的计数就减一走人,post 的动作是加一放行。E2 拿它当了纯互斥用,三轮的成绩同样是全中,中位的耗时是 58.1 ms,比互斥的版本还慢一点,计数的能力没使上,开销倒是一分不少。咱们要是只想让单个计数器原子、又不想进锁,换成 `std::atomic` 的 fetch_add 就收工,它编出来的,就是咱们在汇编里看到的 lock addl。单个变量的原子性咱们已经拿到了,真正考验原子手艺的地方,落在了数据结构上,到后面的招牌实验里,咱们来真的。

## 两条途径:匿名映射配 fork,命名对象配陌生人

咱们把话题收回到相认上:共享内存要进一个进程的时候,门路其实是有两条的。E3 把它们摆进了同一场实验做对照。

咱们看的途径 A,是匿名的共享映射:`MAP_SHARED` 配 `MAP_ANONYMOUS`,fd 传的是 -1,不挂任何的文件。它是没有名字的,唯一的传播方式是 fork 继承:fork 之前建好的映射,子进程会原样地继承一份,写起来是立刻互通的。fork 之后各建各的匿名映射,就是两个互不相干的实体了,匿名的东西没有名字可寻,别的进程也就无从相认了。E3 的输出,把两件事一并演了:

```text
== 途径 A:匿名 MAP_SHARED|MAP_ANONYMOUS(fork 前映射,子进程继承)==
父进程读 fork 前的共享映射:0xC0DE(子进程写的,继承链通了)
父进程读 fork 后自己建的匿名映射:0x0(子进程那份是另一个实体,匿名没有名字无从相认)
```

咱们接着看,子进程往 fork 之前的映射里写了 0xC0DE,父进程读到了,继承链是通的。子进程随后又建了一片自己的匿名共享映射,写进去的是 0xDEAD,父进程在自己 fork 后建的那片里,读到的还是 0。两片映射的调用一个标志位都不差,差的就是 fork 前后的次序。E2 用的正是途径 A,父子三人共用的是同一片。

途径 B 咱们就更熟了,它是本篇的主角:命名对象。server 和 client 是 run_e3.sh 一前一后拉起来的两个独立进程,相互之间是没有任何父子关系的,靠的就是 `shm_open("/lm03_two")` 这个名字相认。server 建好了对象写消息,把一个 atomic 的标志置 1,client 也按名字打开了对象,再靠轮询等标志的变化,读到了消息就回一个 ack:

```text
--- 命名对象:server 与 client 是两个独立进程(无 fork 亲缘)---
[server pid=41045] shm_open 建好 /lm03_two,写消息并置 msg_ready,等 ack(10s 超时)...
[client pid=41047] 与 server 无亲缘,靠 shm_open("/lm03_two") 相认,读到 "hello-from-server-pid-41045",回 ack
[server] 收到 ack,消息送达;unlink 收尾
```

咱们再留意一下 pid:它们俩之间是没有任何亲缘关系的,相认靠的只有名字。命名对象的广播力也就显出来了:它不要求两个进程沾亲带故,启动的时点也不要求同步,client 就算启动得晚了也不怕,咱们让它按名字重试一百次,每回歇 100 毫秒的节奏,总能等到 server 把对象建好的那一刻。

咱们还有第三条路:fd 本身,也是能拿来当信物传的。UNIX 域套接字的 sendmsg 配上 SCM_RIGHTS(辅助消息里塞 fd 的机制),能把一个已打开的 fd 直接递给另一个进程,对方 recvmsg 拿到的,是内核替它复制出来的新 fd,指向的是同一个实体。E3 附带的 fdpass 演示里,子进程收到传来的 fd,mmap 之后读到的就是父进程写的原文,fd 传到了,实体也就传到了,连名字都是不需要有的。细讲的活留在 IPC 篇,memfd 一节咱们还会回来找它。

## 招牌实验:SPSC 无锁环形队列

锁是能保平安的,44.7 ms 的价咱们也量过了。真到高性能 IPC 的现场,大家用的多半是另一副骨架:无锁的环形队列。E4 讲的就是它:一个生产者对一个消费者的配置,英文的名字叫 single producer single consumer,缩写就成了 SPSC,队列整片住在 shm_open 出来的实体里,两个进程之间是一把锁都没有的。

队列的骨架咱们看代码。头部的位置是一小块控制区:起跑线、就绪标志、统计数字。而再往后,是两个各占一条 cache line 的计数器:head 和 tail。cache line 咱们交代一下,它是 CPU 缓存与内存之间搬运的最小单位,x86-64 上一条的大小是 64 字节。咱们用 `alignas(64)` 让 head 和 tail 各自独占一条缓存行,挤在一起的事是没有的。要是不这么排,两个进程轮流写同一条缓存行里的两个变量,硬件会把对方刚写热的副本作废,白白地扔掉带宽,这个现象大家叫它伪共享,本卷讲对齐与大页的那一篇,会专门回来量它的。再往后的,是 1024 个槽、每槽一条 32 字节的消息:

```cpp
// e4_spsc.cpp(节选):head/tail 各占一条 cache line,防伪共享
struct alignas(64) ProdLine { std::atomic<uint64_t> head; char pad[64 - sizeof(std::atomic<uint64_t>)]; };
struct alignas(64) ConsLine { std::atomic<uint64_t> tail; char pad[64 - sizeof(std::atomic<uint64_t>)]; };

struct Header {
    std::atomic<uint32_t> start;   // 起跑线
    std::atomic<uint32_t> ready;   // 消费者就绪
    uint64_t nmsgs;                // 计划发送条数
    uint64_t received, order_err, corrupt;  // 消费者填,父进程收尾读
    int32_t consumer_pid;
    ProdLine p;
    ConsLine c;
};
struct Msg { uint64_t seq; uint64_t checksum; uint64_t payload[2]; };

// lock-free(address-free)原子操作跨进程才成立——静态断言把关
static_assert(std::atomic<uint64_t>::is_always_lock_free);
```

head 的写权只归生产者,tail 的写权只归消费者,这是 SPSC 全部正确性的地基:每个计数器永远只有一个写者。双方的沟通全靠内存序:生产者写完槽,以 release 的语义存 head,等于宣布消息齐了,消费者拿 acquire 的语义读 head,读到的 head 之前的槽,保证已经写完了。反方向的 tail 同理。acquire/release 到底保证了什么,vol5 并发卷的[内存序详解](../../../../vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md) 从头讲过,咱们这里只做工程落地,不重开理论课了。跨进程的路子上,还有一条硬性的前提,就是代码里的 static_assert:某个平台要是把 atomic 实现成带内部锁的,锁就放在了库自己的地界,另一个进程是根本够不着的,只有 lock-free 的原子操作是地址无关的,同一片内存在两个进程里地址不同,它也是照样能工作的。

两边的核心循环,咱们连着背压策略一起看:

```cpp
// e4_spsc.cpp(节选):满/空时的等待策略二选一
const auto backoff = [yield_mode] {
    if (yield_mode) sched_yield();   // yield 版:让出 CPU
    else cpu_relax();                // spin 版:x86 的 pause 指令
};

// ---- 生产者(父进程):满则等(背压),写槽,head 以 release 发布 ----
uint64_t head = 0;
for (uint64_t seq = 0; seq < n; ++seq) {
    while (head - h->c.tail.load(std::memory_order_acquire) >= kSlots) backoff();
    Msg& m = slots[head & (kSlots - 1)];    // 1024 是 2 的幂,掩码取模
    m.seq = seq; m.checksum = mix64(seq);   // 载荷加校验和
    ++head;
    h->p.head.store(head, std::memory_order_release);
}

// ---- 消费者(子进程):空则等,核对序号与校验和,tail 以 release 发布 ----
uint64_t t = 0;
for (uint64_t expected = 0; expected < ch->nmsgs; ++expected) {
    while (t == ch->p.head.load(std::memory_order_acquire)) backoff();
    const Msg& m = cslots[t & (kSlots - 1)];
    if (m.seq != expected) ++got_order_err;         // 乱序计数
    if (m.checksum != mix64(m.seq)) ++got_corrupt;  // 损坏计数
    ++t;
    ch->c.tail.store(t, std::memory_order_release);
}
```

判满判空用的都是单调计数:满的条件是 `head - tail >= kSlots`,空的条件是 `tail == head`。咱们的 head 和 tail 都不回卷,而是一直往上加,取槽位的时候拿 1024 的掩码取模,无符号数的回卷,恰好把减法做对了。一个 uint64_t 的计数器,咱们发的是一百万条,离 uint64_t 绕回零点所需的约 1.8e19 条,差着十三个数量级的距离,余量是管够的。

```text
== E4 SPSC 无锁环形队列:两进程 1,000,000 条 × 32 B,各 3 轮 ==

--- spin(满/空时的等待策略:CPU 自旋 + pause)---
spin  N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41159) 耗时 6.7 ms,148.16 百万条/秒,有效载荷 4521.5 MiB/s
spin  N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41161) 耗时 6.1 ms,163.97 百万条/秒,有效载荷 5003.9 MiB/s
spin  N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41166) 耗时 7.0 ms,142.28 百万条/秒,有效载荷 4342.1 MiB/s

--- yield(满/空时的等待策略:sched_yield 让出)---
yield N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41169) 耗时 3.8 ms,264.33 百万条/秒,有效载荷 8066.7 MiB/s
yield N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41171) 耗时 3.7 ms,267.57 百万条/秒,有效载荷 8165.4 MiB/s
yield N=1000000 槽=1024×32B(共 32960B 实体):received=1000000 order_err=0 corrupt=0(子进程 pid=41173) 耗时 4.0 ms,248.63 百万条/秒,有效载荷 7587.6 MiB/s
```

咱们看正确性:received 收满了一百万,乱序与损坏的计数双双为零,两版的结果完全一致。消费者每收的一条,都要核对它的序号和校验和,release/acquire 的承诺,在两个进程之间兑现了。有意思的是速度:spin 版的中位是 148.2 百万条每秒,yield 版的中位是 264.3,让出 CPU 的那版反而快了六成以上。咱们直觉里,自旋是最快的,货一到手就该立刻拿到嘛,而数字没给这个直觉留面子。

咱们来找原因。要查的头一个嫌疑是调度:两个进程要是老被排到同一个核上的话,自旋就是纯粹的干烧,把自己该跑的时间也搭了进去。钉核对照(E4 的附实验,父进程钉 CPU0、子进程钉 CPU1)把这个嫌疑排除掉了,spin 的中位是 161.7,yield 的中位是 266.3,在两个核各自固定的前提下,差距还是留在原地的。真凶出在了缓存上,也就是咱们隔 cache line 时提过的那个现象:满载自旋的生产者,一遍一遍 acquire-load 的 tail,恰好是消费者每收一条都要写的变量。两个核心轮流要独占的请求,落在了同一条缓存行上,它在两核之间来回地失效再重取,这就是行话里的 cache line 乒乓,真正传消息的带宽,被这一来一回吃掉了。sched_yield 让生产者在队列满的时候安静下来,消费者就有了攒一口气连收一批的机会,tail 所在的缓存行安安稳稳待在消费者那边,乒乓停了,吞吐反而上去了。

它的适用范围咱们划清楚:量的是吞吐。论单条消息的延迟,自旋的响应最快,让出 CPU 的那一档,得等调度器把进程又请了回来,延迟是必输的。吞吐的场景咱们选 yield,延迟的场景就得另外算了,队列的深浅、消费速度跟不跟得上,都会改变答案的,您拿自己的负载量一遍才算数。

## 搬 1 MiB:7304 对 2349

咱们光问队列快不快,还只是事情的一半,选型的时候,您真正要问的是:跟现成的 IPC 比,快多少?E5 让同一份负载走了两条路。负载是 1 MiB 的体量,切成 1024 块、每块 1024 字节的碎片,两边都是逐块校验的。路一是 E4 的队列换大槽(1024 字节 × 256 槽),路二走的是 pipe:父进程 write 一块、子进程 read 一块,同样是 1024 次的写加 1024 次的读,咱们不让任何一边作弊:

```text
== E5 同一 1 MiB:共享内存环形队列 vs pipe 分 1024 次写读,各 3 轮 ==

shm    1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.136 ms → 7343 MiB/s
shm    1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.137 ms → 7304 MiB/s
shm    1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.255 ms → 3917 MiB/s

pipe   1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.426 ms → 2349 MiB/s
pipe   1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.461 ms → 2169 MiB/s
pipe   1 MiB(1024 B × 1024 块):received=1024 corrupt=0 耗时 0.418 ms → 2390 MiB/s
```

咱们把中位数摆开:shm 的中位数是 7304 MiB/s,pipe 的中位数是 2349 MiB/s,差距落在了三倍开外。shm 的第三轮掉到了 3917,咱们按中位数口径取值,单轮的抖动,掩盖不了量级。差距的来路,咱们心里要有数:pipe 的每一块,生产者的一次 write 把数据从用户空间拷进内核,消费者的一次 read 再把它拷回来,一来一回的功夫,花的就是两次系统调用加两次拷贝。而 shm 这边,生产者写的就是消费者要读的内存本身,写完发布 head 就完事了,既没有一次的拷贝,也没有一次的系统调用。快出来的部分,就是省下来的两次拷贝。

当然,pipe 也不是白给的:它给了边界和阻塞的语义,read 一次拿到的就是一块,成帧是天然的,缓冲的管理在两端全免。共享内存的什么都得自己来,队列、同步、成帧、背压的活,全套手艺咱们这一篇练的就是它。真到了工程里,消息小而频、吞吐吃紧,shm 队列是那一头的答案。消息大而松,或者要跨机器的场合,pipe 和套接字就省心得多了。

## memfd_create:不留名字的共享内存

咱们用下来,命名对象的好用是真的,名字带来的负担也是真的:全局的名字空间谁都能看见,冲突、权限、忘了清理,桩桩都成了事。Linux 3.17 起给了另一条路:memfd_create,造的是一个匿名的文件,交给咱们的是一个 fd。咱们说它匿名,说的是它在任何目录里都没有名字,连 `/dev/shm` 里都没有它的身影。咱们说它是文件,ftruncate、mmap、read/write 的接口全都好使,内容是待在内存里的,行为和一个 tmpfs 文件是没有区别的。E6 演了一遍:

```text
== E6 memfd_create:匿名内存的 fd 化 ==

[1] memfd_create("lm03_memfd", MFD_CLOEXEC) = fd 3
[2] ftruncate 4096 + mmap MAP_SHARED + 写入 "hello-memfd" 完成
  readlink(/proc/self/fd/3) -> /memfd:lm03_memfd (deleted)
$ ls -l /dev/shm
total 0
    ↑ /dev/shm 里没有它:memfd 不占名字空间,名字只是 /proc 里的标签
[3] 同名再建一个 memfd(fd=4):两个实体互不影响——fd1 处是 "hello-memfd",fd2 处是 "second-object"
  readlink(/proc/self/fd/4) -> /memfd:lm03_memfd (deleted)
[4] 收尾:close 两个 fd,引用计数归零对象即消失——没有 shm_unlink 这一步……
```

咱们看三处。readlink 给出的路径是 `/memfd:lm03_memfd (deleted)`,man 2 memfd_create 的说法很明白:名字只作调试用途,显示在 `/proc/self/fd` 的符号链接里,永远带着 memfd: 的前缀,对行为是没有任何影响的。咱们同名再建一个,得到的就是两个互不相干的实体,和 E1 里 unlink 后重建的,走的是同一个道理,连 `(deleted)` 的长相都一样。生命周期更是干脆:没有 unlink 的步骤,close 到引用计数归了零,对象就当场消失了,名字跟着一起没了。想给别的进程用?fd 本身就是全部信物,SCM_RIGHTS 一传就到了,或者干脆走 fork 的继承。man 页还给了一个咱们本篇用不上的正经用途:fcntl 的文件封印(file sealing),能把共享内存封到不许对方再截短的程度,防的就是 L02 里 SIGBUS 那一类事故,您到需要防不可信对端的时候,您再回来翻它就是了。

用它的门槛,咱们交代两行:内核的门槛是 3.17 起,glibc 的包装函数 2.27 起。man 页的口径是编译前 `#define _GNU_SOURCE`,那是针对 C 编译器的。咱们用的 g++ 在 Linux 上本来就定义了它,存档的编译行没带也过,头文件用的还是 `<sys/mman.h>`。

那咱们什么时候用它,什么时候又用 shm_open?咱们理解下来是这样:shm_open 的名字是公开的,它适合的是松耦合,今天启动的进程,按名字能找到昨天注册的对象,认识的活儿交给名字去干。memfd 是点对点的,fd 交给了谁,谁就有了使用权,没拿到 fd 的进程,连它的存在都不知道,全局的名字是一点不占的,权限的归属跟着 fd 走。安全敏感、生命周期跟着某一条连接走的场景,memfd 顺手的程度还会更高。

## 另一侧怎么看

咱们再看 Windows:那边是没有 shm_open 的,同一件事走的是 CreateFileMappingW。头一个参数本该传的是文件句柄,咱们把 INVALID_HANDLE_VALUE 传进去,映射就不挂任何文件了,拿系统的页面文件(page file,Windows 的虚拟内存后备文件)当存储,这就是 Win32 共享内存的惯用形态,[文件映射](../../windows/file-io/02-file-mapping.md) 那篇(W02)已经把它当映射对象讲过一遍了。镜像篇[共享内存:页面文件后备的命名映射对象](../../windows/memory/02-shared-mem.md) 已经落稿,拿它和本篇逐行对拍过了,咱们这里只预告寿命上最值得记的一处分岔。两侧其实是同构的:对象都是活到最后一份引用的,Linux 这边的 fd 与映射都算,Windows 那边的句柄与视图都算。真正的分岔,出在名字的死法上:咱们这边 unlink 是显式的,名字的生死与映射的多少不相干。那边是没有 unlink 可调的,名字挂在了系统的对象命名空间里,跟着句柄的计数走,而最后一个句柄一关,名字当场就摘了,对象还能靠视图匿名地续命到最后一个引用。共享与同步的正文,咱们到镜像篇再会。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="shm_open(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/shm_open.3.html"
  />
  <ReferenceItem
    :id="2"
    title="shm_unlink(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/shm_unlink.3.html"
  />
  <ReferenceItem
    :id="3"
    title="pthread_mutexattr_setpshared(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/pthread_mutexattr_setpshared.3.html"
  />
  <ReferenceItem
    :id="4"
    title="sem_init(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/sem_init.3.html"
  />
  <ReferenceItem
    :id="5"
    title="memfd_create(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/memfd_create.2.html"
  />
  <ReferenceItem
    :id="6"
    title="ftruncate(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/ftruncate.2.html"
  />
  <ReferenceItem
    :id="7"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="8"
    title="capabilities(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/capabilities.7.html"
  />
</ReferenceCard>
