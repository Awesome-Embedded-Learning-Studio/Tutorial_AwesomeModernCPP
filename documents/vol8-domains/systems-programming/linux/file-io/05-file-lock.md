---
title: "文件锁:flock 与 fcntl 记录锁"
description: "两个进程同时碰一个文件,谁让谁:本篇把 flock 与 fcntl 记录锁两套并存的机制放到同一块 ext4 上对拍——flock 的锁挂在打开文件描述上(同进程重新 open 会自冲突、阻塞版等 2 秒被 alarm 击杀取证、close 最后一个引用才释放、fork 出的子进程能替父放锁),fcntl 的锁挂在进程上(字节区间生效、F_GETLK 报的是对方锁自己的区间、后锁把重叠段切成 W[0,50)+R[50,150)),以及全篇最大的陷阱 E2d:同进程 close 该文件的任意一个 fd,全部记录锁当场释放(锁后才 open 的、加锁之前就 open 好的都一样),而 Linux 3.15 起的 F_OFD_SETLK 没有这个问题(l_pid 报 -1、关对描述才释放);/proc/locks 逐字段解码含十进制 inode 的实测教训、fork/dup/exec 继承对照主表、RAII file_lock 的 try_lock_for 轮询时序、8 进程串行化 8090.5 ms 对 1009.9 ms 与 16 µs 次交接的价、tmpfs 逐行一致的 VFS 层佐证,NFS 未测如实标注"
chapter: 8
order: 5
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 21
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "错误处理范式:从 errno 到 expected"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - mutex
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 文件锁:flock 与 fcntl 记录锁

前四篇走下来的时候,文件这一侧咱们一直是单机作业,open、read、mmap、目录遍历之类的操作,一个进程自己就全办了。哪怕 [L02](./02-mmap-memory-mapping.md) 的实验里出现过两个进程共享同一份页缓存,大家也只是互相看得见对方写下的字节,谁也不挡谁的道。这一篇咱们把第二个进程真正放进场:题目变成了两个进程抢写同一个文件,A 写到一半的时候 B 进来了,怎么办?[L01](./01-posix-file-io.md) 其实发过两件小工具,可惜全都只管各自的一小段。`O_APPEND` 做的是把“挪偏移到文件尾+写入”合并成了一次原子步骤,它保证的只是追加那一瞬间不互相覆盖。`O_CREAT|O_EXCL` 做的是把“查存在+创建”合并成了一次原子操作,它保证的只是抢建锁文件时只有一个赢家。A 要改的是文件的整整一段,改完之前 B 是不许碰的,这样的跨进程互斥,两件小工具都给不了。能补上这块的,就是文件锁了。

Linux 的手里有两套,咱们都得认。`flock(2)` 是 BSD 的血统,API 只有孤零零的两个参数,锁的对象是整个文件,man 页对它的定位,就是对一个打开的文件加或撤一把咨询锁(advisory lock)。`fcntl(2)` 的记录锁(record lock)走的是 POSIX 标准,经由我们熟悉的 fcntl 加一堆 `F_` 开头的命令字,能锁的是任意字节区间。两套的样子很像,干互斥的活都干得了,可咱们问一句“锁到底属于谁”,两套给出的答案不一样。flock 的锁挂在**打开文件描述**身上,man 页的原文是 open file description,[L01](./01-posix-file-io.md) 讲 dup 的时候介绍过它,那是系统级的对象。fcntl 的记录锁则挂在**进程**身上。close、fork、exec、dup 的全部行为差异,都是这一句的推论。本篇咱们就沿着主线做实验,把两套锁的语义矩阵,一组一组地实测到底。

咱们还得在动手前交代一层性质,两套锁给的都是咨询锁(advisory lock)。咨询锁只挡同样调锁的进程,A 拿了锁之后,B 的加锁请求就得等。可要是换了个根本不调锁的 B,直接甩了一句 `write(fd, ...)` 过去,内核是不会拦它的。man 5 proc_locks 的字段表里还留着 MANDATORY(强制锁)的位置,强制锁倒是真能把不合作的读写也拦下来,不过 Linux 的实现早已废弃,咱们不去碰它。所以咨询锁的约束力,只覆盖参加进来的各方,绕开锁直接读写的人,锁是管不着的,这也是它跟线程里的 mutex 最不一样的地方。

本篇的实验按 E1 到 E7 编号,与仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/05-file-lock/` 下的 01 到 07 七个目录一一对应,代码与全部的原始输出都收进了存档,您随时可以对表。环境口径咱们也照例交代清楚:台机用的是 AMD Ryzen 7 9700X(8 核 16 线程),WSL2 的内核 6.18.33.2-microsoft-standard-WSL2,g++ 16.2.1 的工具链,咱们一律用 `-std=c++20 -O2 -Wall -Wextra -Wpedantic` 编译,拿到的是零警告。数据文件放在 ext4 的 `/home/charliechen/l05_scratch/eN/` 下(/dev/sdd,它的设备号 8:48 是 E3 还要出场的数字),路径烧死在各源码开头的常量里,复跑之前您得把目录建出来。老三件 `unique_fd`、`sys_call`、`errno_code` 沿用 [RAII 篇](../../thinking/01-raii-paradigm.md)和[错误处理篇](../../thinking/02-error-paradigm.md)的定义,咱们只引用。另外还借了一个把 errno 值译成名字的小函数 `errno_name`,它定义在 01 号实验的源码里。多进程的时间线还有个观察纪律要说在前面:实验里的子进程一律 `_exit()` 退场,它是不刷 stdio 缓冲的([L03](./03-page-cache.md) 的场景 C 亲手量过这个亏),所以每个实验的开头都有一句 `setvbuf(stdout, nullptr, _IONBF, 0)`,谁拿到了锁谁落笔,时间戳才作不了假。

## E1 flock:锁挂在打开文件描述上

咱们从 flock 起步,它的全部签名就是 `int flock(int fd, int operation)`。operation 的取值从 `LOCK_EX`(独占)与 `LOCK_SH`(共享)里挑锁型,需要的时候再或上 `LOCK_NB`(非阻塞)与 `LOCK_UN`(放锁)。man 2 flock 的头一句话就把归属说死了:锁认的东西只有 open file description,也就是前文的打开文件描述,fd 号与进程都入不了它的眼。咱们把 [L01](./01-posix-file-io.md) 讲 dup 时说过的两级结构再摊开:进程私有的 fd 表,每一项都指向系统级的打开文件描述。`open` 出来的两次,得到的是两个独立的描述。`dup` 和 `fork` 复制的是表项,于是两个 fd 就指向了同一个描述。flock 的全部脾气,咱们都能从这套结构推出来。

道理推完了,咱们还是要眼见为实。E1 一共排了五组观察,出场的角色有两类:持锁者 A 与竞争者。竞争者是自己去 `open` 的,这一点 E1e 会解释——继承来的 fd 是测不出互斥的。它的骨架长这样:

```cpp
// flock_matrix.cpp(节选):竞争者子进程,自己 open 拿全新描述,非阻塞试锁
pid_t spawn_contender()
{
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        int r = ::flock(own.get(), LOCK_EX | LOCK_NB);
        std::printf("  [%6ld ms] 竞争者(pid=%d, 自己open):flock(LOCK_EX|LOCK_NB) = %d",
                    ms(), ::getpid(), r);
        if (r == -1) {
            std::printf(", errno=%d(%s)", errno, errno_name(errno));
        }
        std::printf("\n");
        ::_exit(0);
    }
    return pid;
}
```

五组观察的原始输出咱们直接看,时间戳是相对程序启动的毫秒数:

```text
$ ./e1
pid=152060, 数据文件:/home/charliechen/l05_scratch/e1/flock.bin(ext4)

==== E1a  LOCK_EX 互斥:第二进程阻塞,解锁瞬间接棒 ====
  [     0 ms] A(pid=152060):flock(LOCK_EX) = 0,持锁
  [   400 ms] A:LOCK_UN,放锁 → C 的拿锁时刻应紧贴这一行
  [   400 ms] C(pid=152062):自己 open + 阻塞 flock(LOCK_EX) 返回 0,拿到锁

==== E1b  同一进程两次 flock:同 fd / dup / 重新 open ====
  [   400 ms] fd1: flock(LOCK_EX) = 0
  [   400 ms] fd1 再 flock(LOCK_EX) = 0(同一描述,重复加锁即转换)
  [   400 ms] fd1 再 flock(LOCK_SH) = 0(同一描述上 EX→SH 转换)
  [   400 ms] dup(fd1) 上 flock(LOCK_EX|LOCK_NB) = 0(dup 共享同一描述,不冲突)
  [   400 ms] 竞争者(pid=152069, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [   401 ms] 重新 open 的 fd2:flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [   401 ms]            → 同进程两个描述也会自冲突
  [   401 ms] 子进程对 fd2 用阻塞版 flock,2 s alarm 护航…
  [  2401 ms] 子进程被 SIGALRM 击杀:阻塞版 flock 2 s 未返回 → 自锁死

==== E1c  LOCK_NB:冲突时 -1 + EWOULDBLOCK ====
  [  2401 ms] 竞争者(pid=152083, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)

==== E1d  close(fd) 隐式释放:A close 后 B 立刻拿到 ====
  [  2402 ms] A 持锁
  [  2402 ms] 竞争者(pid=152084, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [  2402 ms] A close(fd)
  [  2402 ms] 竞争者(pid=152085, 自己open):flock(LOCK_EX|LOCK_NB) = 0

==== E1e  fork 继承:锁属于打开文件描述,不属于进程 ====
  [  2402 ms] 父:持锁(fork 前)
  [  2403 ms] 子(pid=152086):继承 fd 上 flock(LOCK_EX) = 0(同一描述,不与父冲突)
  [  2403 ms] 子:LOCK_UN = 0(放的是整个描述上的锁)
  [  2403 ms] 竞争者(pid=152087, 自己open):flock(LOCK_EX|LOCK_NB) = 0
  [  2403 ms] 父:再次持锁
  [  2403 ms] 子(pid=152088):close(继承 fd)
  [  2404 ms] 竞争者(pid=152089, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [  2404 ms] 竞争者(pid=152090, 自己open):flock(LOCK_EX|LOCK_NB) = 0
```

E1a 是基准款:A 拿了锁以后睡 400 毫秒,期间竞争者 C 用阻塞版的 `flock` 干等。A 一句 `LOCK_UN` 落地,C 在**同一毫秒**拿到了锁。互斥这件事咱们不意外,值得记的是时序的贴合:放锁到接手之间没有看得见的缝隙,到 E6 咱们再给它量价。E1b 就有意思了,咱们一行行读。同一个 fd 上的第二次 `flock(LOCK_EX)` 返回了 0——man 页说得很清楚,同一描述上的重复调用是**转换**,并非添了第二把锁,连 EX 到 SH 的降级也这么走(man 同一段还提醒了,转换是不保证原子的:内核的做法是放掉旧的再拿新的,中间是有窗口的)。`dup` 出来的 fd 立即成功,因为 dup 复制的是指向同一描述的新表项,man 的原话是 “refer to the same lock”。

重新 open 的那两行,才是 flock 最容易翻车的语义:fd2 是同一进程自己 open 的,试锁照样吃了 -1 加 EWOULDBLOCK。同一个进程里的两个描述,自己把自己挡住了。man 2 flock 把这个行为写在了明面上:多个 fd 是各自独立对待的,拿其中一个 fd 上的锁,可能被同一个进程经另一个 fd 持有的锁拒绝。阻塞版会怎样?咱们没有干等,派了个子进程对 fd2 调阻塞版 `flock`,再挂一个 2 秒的 alarm 收尸。2401 毫秒处的输出告诉我们:子进程被 SIGALRM 击杀,阻塞版的调用从未返回。持锁的 fd1 与等待的 fd2 在同一个进程手里,放锁的人永远不会出现,这就是咱们说的“自己挡自己”,而且是完整的形态。

E1c 补齐了错误口径:带 `LOCK_NB` 的调用在冲突时返回 -1,errno 的值是 11,也就是咱们见过的 EWOULDBLOCK,它与 Linux 上的 EAGAIN 同值,您在 fcntl 那边看到 EAGAIN,念成同一个词就行了。E1d 演的是隐式释放:A 从头到尾没调过 `LOCK_UN`,只做了一次 `close(fd)`,竞争者就在同一毫秒拿到了。“描述的最后一个引用被关闭”,这件事才是真正放锁的动作。E1e 是收官的一组,咱们在这组把语义看得最清:子进程经 fork 继承了 fd,在继承的 fd 上 `flock(LOCK_EX)` 直接返回了 0:父与子指到的是同一个描述,共享的是同一把锁,冲突是谈不上的。更妙的是子进程的一句 `LOCK_UN`,放掉的是**父进程的锁**,下一个竞争者立刻就拿到了。咱们反过来再看,子进程只 `close` 继承的 fd,锁还稳稳地待在原处,因为父进程手里的 fd 仍引用着那个描述,等父进程也关掉了,竞争者这才拿到了。“锁属于描述”,E1e 把这句话从三个方向各敲了一次。

## E2 fcntl 记录锁:字节区间,属主是进程

咱们换到 fcntl 这套,面貌立刻就不同了。flock 那边锁的是整个文件,fcntl 记录锁锁的对象则变成了一段一段的字节区间,描述它的结构体恰好也叫 `flock`:

```cpp
// fcntl_matrix.cpp(节选):lock_spec 把五元组填进 struct flock
struct lock_spec {
    short type;    // F_RDLCK / F_WRLCK / F_UNLCK
    off_t start;   // 起始偏移
    off_t len;     // 0 = 到 EOF

    ::flock fl() const
    {
        ::flock f{};              // 裸写即可:本文件未含 sys/file.h,无遮蔽
        f.l_type = type;
        f.l_whence = SEEK_SET;    // 也可以 SEEK_CUR / SEEK_END
        f.l_start = start;
        f.l_len = len;
        f.l_pid = 0;              // 只在 F_GETLK 带回的答案里有意义
        return f;
    }
};
```

两处编译的细节,您自己写的时候别绕开。头一处是结构体名与函数名打架:包含了 `<sys/file.h>` 之后,函数 `flock()` 的名字会把类型名遮住,填结构体的时候就得写 `struct ::flock`,取的是全局作用域里的类型。这副写法在存档 03、04、07 三个实验的源码里都能见到,它们都包含了那个头文件。上面 02 号的节选没有包含它,裸写 `::flock` 反而编得过。第二处是 `_GNU_SOURCE`:后面要用到的 `F_OFD_SETLK`,是把锁挂到打开文件描述上的新版记录锁命令(E2d' 节细讲),这批命令字躺在 glibc 的 `__USE_GNU` 段里,而 g++ 编 C++ 时已经替咱们预定义了 `_GNU_SOURCE`,源码里那句 `#ifndef _GNU_SOURCE` 只是防御性的,您要是手动再 `#define` 一遍,反而会吃一个 `-Wmacro-redefined` 的警告。命令字咱们认三个:`F_SETLK` 非阻塞地上锁或解锁,冲突时回的是 -1 加 EAGAIN。`F_SETLKW` 里的 W 是 wait,冲突的时候睡进去等。`F_GETLK` 的角色是探针,问的问题就是“我要锁这段,谁挡我”。

咱们直接看 E2 的各组输出,把区间锁的规则对上(E2d 下一节单独讲):

```text
$ ./e2
pid=153582, 数据文件:/home/charliechen/l05_scratch/e2/fcntl.bin(ext4)

==== E2a  字节区间:A 锁 [0,100),B 试 [50,150) 冲突、[100,200) 成功 ====
  [     0 ms] A(持锁者 pid=153584):F_SETLK F_WRLCK [0,100) = 0
  [     0 ms] B(pid=153585):F_SETLK F_WRLCK [50,150) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     0 ms] B(pid=153586):F_SETLK F_WRLCK [100,200) = 0
  [     0 ms] A 已 _exit(0)(见 E2f:进程退出即释放)
  [     1 ms] B(pid=153587):F_SETLK F_WRLCK [0,50) = 0

==== E2b  F_GETLK:探测冲突锁的 pid 与实际区间 ====
  [     1 ms] A(持锁者 pid=153588) 持 F_WRLCK [0,100)
  [     1 ms] 探针B(pid=153589):F_GETLK F_WRLCK [0,200) → 撞上 F_WRLCK l_pid=153588, 区间[0,100)(r=0)
  [     1 ms] 探针B(pid=153590):F_GETLK F_WRLCK [150,200) → 无冲突(l_type=F_UNLCK)
  [     2 ms] 探针B(pid=153591):F_GETLK F_WRLCK [50,60) → 撞上 F_WRLCK l_pid=153588, 区间[0,100)(r=0)

==== E2c  读共享/写排他:四种组合 ====
  A 持 F_RDLCK [0,100) 时,B 试 F_RDLCK [0,100):  [     2 ms] B(pid=153593):F_SETLK F_RDLCK [0,100) = 0
  A 持 F_RDLCK [0,100) 时,B 试 F_WRLCK [0,100):  [     3 ms] B(pid=153595):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  A 持 F_WRLCK [0,100) 时,B 试 F_RDLCK [0,100):  [     4 ms] B(pid=153597):F_SETLK F_RDLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  A 持 F_WRLCK [0,100) 时,B 试 F_WRLCK [0,100):  [     4 ms] B(pid=153599):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))

==== E2e  替换语义:后锁覆盖重叠段;同进程第二把锁不自冲突 ====
  [     8 ms] 本进程(pid=153582):fd1 上 W [0,100)
  [     8 ms] 本进程:fd2 上再 W [0,100) = 0(POSIX 锁按进程算,永不自冲突)
  [     8 ms] 本进程:fd1 上 R [50,150) = 0(重叠段被替换)
  [     8 ms] 读探针(pid=153609):F_GETLK F_RDLCK [0,50) → 撞上 F_WRLCK l_pid=153582, 区间[0,50)(r=0)
  [     8 ms] 读探针(pid=153610):F_GETLK F_RDLCK [50,150) → 无冲突(l_type=F_UNLCK)
  [     8 ms] 写探针(pid=153611):F_GETLK F_WRLCK [50,150) → 撞上 F_RDLCK l_pid=153582, 区间[50,150)(r=0)
…(收尾清场的对照探针一行,略)…

==== E2f  进程退出释放:持锁者 _exit,竞争者立刻拿得到 ====
  [     9 ms] A(持锁者 pid=153613):持 W [0,100)
  [     9 ms] 竞争者(pid=153614):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [    10 ms] A 已 _exit(0),没有任何放锁动作
  [    10 ms] 竞争者(pid=153615):F_SETLK F_WRLCK [0,100) = 0
```

咱们把 E2a 的三行试锁对一对:冲突与否看的是区间有没有重叠,`[50,150)` 与 `[0,100)` 交叠了 50 个字节,所以挡了。`[100,200)` 您再量一量,是挨着但不相叠的,所以过了。这正是记录锁胜过整文件一把锁的地方:一个文件的不同区域可以由不同进程同时各管一段,读者多的场景按段分片,吞吐就上去了。E2a 末尾的两行还捎带了 E2f 的答案:A 一退场,B 立刻拿到了 `[0,50)`。

E2b 教咱们用 F_GETLK 看清对手:探针填上自己想锁的类型与区间,咱们拿它去调用,判据是返回的 `l_type`——被改成了 `F_UNLCK`,说明没人挡咱们。结构体里留下来的,就是**对方那把锁**的信息,类型、属主 pid、区间都在里面了。有个细节值得您多看一眼:探针只伸进 `[50,60)` 十个字节,报回来的区间还是人家自己的 `[0,100)`。它报的是对方锁的完整区间,交集得咱们自己拿它减出来。输出块里 E2e 的读探针就是跨型的场合:探的是 R,报回来的 `l_type` 是 W,咱们照样读得出对手。

E2c 的四种组合排下来,咱们看到的规则与读写锁同构:读读是共存的,凡带写的都冲突。E2f 演的是进程的退场:持锁子进程直接 `_exit(0)`,放锁、关 fd 的事一样都没做,竞争者照样立刻拿到了——锁挂在进程身上,进程没了,锁自然就没了。

E2e 里藏着的语义有两个,咱们配合三个探针读。头一个:fd2 是同进程重新 open 的,经它再上 W `[0,100)` 居然返回了 0——POSIX 记录锁以进程为单位,同一进程经任何 fd 提的锁请求都并入自己名下,自冲突是不会有的。您把这句跟 E1b 对照:flock 重新 open 自相阻塞,到了 fcntl 那边,重新 open 就若无其事了,两套机制在同一个小实验上给出了相反的答案。另一个语义是替换:同一进程后来上的 R `[50,150)`,把重叠段 `[50,100)` 从写锁换成了读锁,内核把原来的 W `[0,100)` 切成了 W `[0,50)` 加 R `[50,150)` 两段。咱们用三个探针逐段验收:读探针在 `[0,50)` 读到的是 W 挡路,在 `[50,150)` 就畅通无阻了,写探针在 `[50,150)` 读到的全是 R。同进程的锁请求之间不存在协商,后到的会直接改写重叠段。

## E2d:close 任意一个 fd,全部记录锁当场释放

到全篇分量最重的一组了。您不妨猜一下:同一个进程里,fd1 上好了 W `[0,100)`,此刻另有一段与锁无关的代码 open 了同文件的 fd2,用完了随手 close——fd1 还开着,咱们连一次 `F_UNLCK` 都没调过,锁还在吗?按“锁挂在 fd 上”的直觉,您多半觉得它还在。按 E1d 教过的“关最后一个引用才释放”,咱们更觉得它该在。咱们看实测输出:

```text
==== E2d  陷阱:同进程 close 任意一个 fd → 全部记录锁释放 ====
  [     5 ms] 本进程(pid=153582):fd1 上 F_SETLK W [0,100) = 0
  [     5 ms] 竞争者(pid=153600):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     5 ms] 本进程:open 第二个 fd(只读都行),随即 close(fd2)
  [     5 ms] 本进程:fd2 已 close,fd1 还开着,一次 LOCK_UN 都没调
  [     5 ms] 竞争者(pid=153601):F_SETLK F_WRLCK [0,100) = 0
…(存档里这组还排了反过来的次序:close 加锁之前就 open 的 fd,四行输出同型,略)…

==== E2d'  对照:F_OFD_SETLK(锁挂在打开文件描述上)没有这个陷阱 ====
  [     6 ms] 本进程:fd1 上 F_OFD_SETLK W [0,100) = 0
  [     6 ms] 探针(pid=153604):F_GETLK F_WRLCK [0,100) → 撞上 F_WRLCK l_pid=-1, 区间[0,100)(r=0)
  [     7 ms] OFD探针(pid=153605):F_OFD_GETLK F_WRLCK [0,100) → 撞上 F_WRLCK l_pid=-1, 区间[0,100)(r=0)
  [     7 ms] 竞争者(pid=153606):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     7 ms] 本进程:open 第二个 fd 并 close —— 锁不受影响?
  [     7 ms] 竞争者(pid=153607):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     7 ms] 本进程:close(持锁的 fd1)
  [     7 ms] 竞争者(pid=153608):F_SETLK F_WRLCK [0,100) = 0
```

答案偏偏是不在。竞争者从被挡到拿到锁的中间,只发生了一件事:close 了一个**不是用来加锁的** fd。而且这跟 fd2 是什么时候 open 的毫无关系。同一组实验还排过一个反过来的次序:有一个 fd 在加锁之前就 open 好了,锁挂在后来新开的 fd 上,关掉早开的 fd,锁照样全放了。POSIX 把这样的语义写进了标准,man 2 fcntl_locking 的原句毫不含糊:关闭了指向该文件的**任意一个** fd,该进程在此文件上的**全部锁**都会被释放,原句里的 any 还特意用了斜体。历史成因咱们不考古,只说后果:记录锁挂在进程的名下,内核得在进程持有的每一个 fd 上捕捉关闭事件,于是任何一个 fd 的关闭,都成了全量释放的触发器。

::: warning 跨进程锁最阴险的静默失效
同进程里随便哪一段无关的代码,只要它 open 又 close 了同一个文件,您名下的记录锁就全没了。读配置的、写日志的、stat 一下就走的代码,个个都可能是您进程里的那个 fd2。咱们能防的只有两招:要么保证整个进程对这一文件只 open 一次、fd 从头攥到尾,要么换用下面的 OFD 锁。多线程程序的正路是另一条:每个线程自己 open、用 F_OFD_SETLK 上锁,锁挂在各自的描述上,谁也误伤不了谁。
:::

E2d' 的对照就清爽了。`F_OFD_SETLK`(open file description lock 的缩写,Linux 3.15 起才有的扩展)把锁的挂靠点从进程挪回了打开文件描述,man 2 F_OFD_SETLK 的对比写得直白:传统记录锁关联进程,OFD 锁关联的是取得它的那个描述,释放的时机则要引 man 的原话,“on the last close”。最后一个引用关闭了才释放,任何一次 close 都够不着这样的锁。输出逐行对上:close 无关的 fd2 之后竞争者依旧被挡,直到 close 了真正持锁的 fd1,锁这才被放掉了。还有个辨认的特征:OFD 锁没有属主 pid,`F_GETLK` 与 `F_OFD_GETLK` 探到它的时候,报的都是 `l_pid=-1`,man 的原话是 “_l_pid_ is set to -1”。您想按 pid 找对手,它告诉您没有对手进程:锁的归属是一个打开文件描述,而不是任何进程。写新代码的时候,除非您要兼容老内核或老平台,区间锁咱们默认上 OFD 版本,E2d 那个陷阱就整个绕开了。

## E3 /proc/locks:把两套锁摊开看

锁在内核里长什么样,咱们有仪器:`/proc/locks`,它把全系统当前的锁列成文本,E1 到 E4 眼里那些 -1 与 0 的背后,在这里全都变得有名有姓了。E3 的布局:一个持锁者给同一文件上两样东西,一样是 flock 的独占锁,一样是 fcntl 的写锁 `[0,100)`。咱们再派两个等待者,分别堵在 flock 与 `F_SETLKW` 的两条等待队列里。主进程趁全场都在的时候,咱们读出 `/proc/locks`,按 inode 过滤出本文件的行:

```text
$ ./e3
pid=167262, 文件:/home/charliechen/l05_scratch/e3/locks.bin
fstat: inode=1690193(十进制), 设备 8:48(十进制) = 08:30(十六进制)

  [     0 ms] 持锁者(pid=167263):flock(LOCK_EX) + F_SETLK W [0,100) 都已上身
[   300 ms] /proc/locks 中 inode 1690193 的行:
---- 锁全部在场 + 两个等待者被挡 ----
19: POSIX  ADVISORY  WRITE 167263 08:30:1690193 0 99
19: -> POSIX  ADVISORY  WRITE 167265 08:30:1690193 50 149
20: FLOCK  ADVISORY  WRITE 167263 08:30:1690193 0 EOF
20: -> FLOCK  ADVISORY  WRITE 167264 08:30:1690193 0 EOF
…(程序随后还打印了四行逐字段解码,与咱们下面的逐字段讲解重复,略)…
  [  1000 ms] 持锁者:退场(两把锁随进程释放)
  [   999 ms] F_SETLKW 等待者(pid=167265):拿到 [50,150),退场
  [   999 ms] flock 等待者(pid=167264):拿到锁,退场

[  1000 ms] 持锁者退场后,两个等待者都拿到了锁(见上)

---- 全部退场后(应为空) ----
(没有命中的行)
```

咱们拿第一行当标本,把字段挨个地解码。开头的 `19:` 是本次读到的序号,每次读取的时候都从头重排,您别拿它当稳定 id。第二字段 `POSIX` 是锁的家族,另两个常见的值是 `FLOCK` 与 `OFDLCK`(OFD 锁)。fcntl 与 flock 两套锁在内核里是分开登记的,互相是不通气的,E3 里同一文件同时挂着的 FLOCK 与 POSIX 就是活的证据。`ADVISORY` 说的是咨询锁。`WRITE` 与 `READ` 是锁的类型。`167263` 给的是属主 pid,OFD 锁在这里显示的是 -1。`08:30:1690193` 给的是设备号加 inode。收尾的两个字段 `0 99` 是锁的字节区间。

区间字段有它的讲究,咱们细看。fcntl 的 l_start=0、l_len=100,咱们嘴里说的是半开区间 `[0,100)`,`/proc/locks` 里印的却是**闭区间的端点** 0 和 99——100 个字节,最后一个字节的偏移是 99。l_len 为 0(锁到 EOF)时右边印的是 `EOF`,flock 家族在这里永远印的是 `0 EOF`。被挡住的等待请求也不单列一行,而是以 `->` 缩进挂在挡路者的序号下面:19 号 POSIX 锁下面挂着 167265 的等待,20 号 FLOCK 的下面挂着 167264。等待者那行印的区间 `50 149`,同样是它自己请求的那一段,道理跟上面 F_GETLK 的报法是一样的。等持锁者退了场,两个等待者在同一毫秒各自拿到了,再读 `/proc/locks` 就干净了。

下面的字段,笔者要专门拦下来讲,因为咱们在这里栽过实打实的跟头:`08:30:1690193` 里,设备号用的是十六进制,inode 用的是**十进制**。man 5 proc_locks 对这个字段的说明只有“冒号分隔的子字段,主次设备号加 inode”,进制是只字未提的。真相在内核 fs/locks.c 的 `lock_get_status()`,格式串咱们原样抄来:`"%d %02x:%02x:%llu"`,设备号走的是 %02x,inode 走的 %llu。实验的第一版拿 `printf '%x'` 转出来的十六进制 inode 去 grep,捞出来是空的,咱们差点当场写下“锁不在 /proc/locks 里”的错误判定。回头拿 fstat 的十进制 `st_ino` 直接对,四行全都命中了。所以您的排查姿势应该是:`stat -c %i 文件` 要十进制的原值,设备号倒是该 `printf '%02x:%02x'` 转成十六进制。另外本机常驻的 dotnet、VS Code 系进程自己也持锁,`/proc/locks` 里混着别人家的行,过滤的时候要认 inode,您别去数总行数。

## E4 fork、dup、exec:谁继承,谁释放

单点的行为咱们都见过了,现在咱们把它们排成一张总表。E4 用同一个二进制跑了六组场景,每组的竞争者都是自己 open 的,输出的原始记录都在存档里,咱们在这里收拢成表,再挑几处实测的原文来背书:

| 维度 | flock(2) | fcntl(2) 记录锁(F_SETLK 族) |
|---|---|---|
| 作用域 | 整个文件 | 任意字节区间,l_len=0 到 EOF |
| 锁属于谁 | 打开文件描述 | 进程;F_OFD_SETLK 变体属于描述(Linux 3.15 起) |
| close(加锁的 fd) | 描述最后一个引用关闭才释放 | 全部记录锁当场释放,别的 fd 上的也一起 |
| close(别的 fd) | 不影响锁 | 同样全释放 |
| fork | 父子共享同一把锁,子可替父放锁 | 不继承,子进程是独立属主,会与父冲突 |
| exec | fd 不关就活着,O_CLOEXEC 则当场释放 | 锁随进程活过 exec |
| 同进程第二次 open | 自冲突,阻塞版永远等不到 | 并入名下,后锁改写重叠段 |
| 探测 | 无,只能试 | F_GETLK 报 pid 与区间;OFD 锁报 l_pid=-1 |
| tmpfs | 与 ext4 同语义(E7 实测) | 与 ext4 同语义(E7 实测) |
| NFS | 客户端模拟成整文件的区间锁 | 有丢锁风险(租约到期) |

fork 那两行的输出原文最有力气,咱们直接引。flock 的一侧,子进程在继承 fd 上加锁返回了 0,子进程的一句 `LOCK_UN = 0 —— 放的是父进程的锁`,竞争者应声拿到了。fcntl 的一侧,子进程用继承的 fd 上锁吃到 -1 加 errno 11,`F_GETLK` 报出的 `l_pid=168327` 正是父亲的 pid——挡它的就是父亲,因为 fcntl 的锁认进程,而 fork 出来的孩子是别人。man 2 fcntl_locking 的一句话同时裁定两行:fork 出的子进程拿不到继承,锁跨过 execve 却留了下来。

exec 的两场咱们也各看一眼。flock 的一侧:持锁子进程 exec 自己(fd 不带 O_CLOEXEC,还调了 `fd.release()`,免得 RAII 的析构抢着把 fd 关掉),竞争者在窗口内是被挡的,exec 后的进程稳稳持有 600 毫秒,一退场就释放了。同一个场景换成带 `O_CLOEXEC` 的 fd,exec 的那一瞬间 fd 被内核关掉了,锁当场就消失了,窗口内的竞争者直接拿到了。fcntl 的一侧:上锁的子进程亲自 exec,竞争者照样是被挡的,因为锁是跟着进程走的,映像换了,主人是没换的。有个小现象咱们要提前打个招呼,免得您对存档时疑惑:exec 之后的子进程时间戳从 0 重新计数,因为 t0 是进程映像里的静态变量,exec 换了映像,钟也跟着换了,这属于预期的行为,您不必把它当成错拍。

NFS 的那两行,咱们如实交代:没测。笔者的 WSL2 里没有 NFS 挂载,表里的说法全部来自 man 页的理论口径:

> man 2 flock 的口径咱们原样抄来:2.6.11 及之前的内核里,flock() 在 NFS 上压根锁不了。2.6.12 起客户端把 flock() 模拟成了对整个文件的 fcntl 字节区间锁,man 的原文还特意用斜体强调 do interact 这件事,说的是两族锁在 NFS 上会互相作用。2.6.37 起 nfs 又给了 `local_lock` 挂载选项,按本地锁的口径处理。
>
> man 2 fcntl_locking 的 NFS 一节还给咱们列了 “lost locks”:服务器那边的管理动作或网络分区,锁是会消失的,Linux 3.12 起丢了锁还继续 I/O 的话,收到的可能是 EIO。NFSv4 的租约默认 90 秒,`nfs.recover_lost_locks` 因数据损坏的风险默认是 0,也就是丢了的锁不还。

跨机器共享文件上的锁,语义比本地的软,您设计的时候得把这层折扣算进去。

## E5 file_lock:把 flock 写进析构函数

机制看得够了,该收进类型里了。fd 是 [RAII 篇](../../thinking/01-raii-paradigm.md)的老客户,这回咱们把“一把 flock 锁的生死”也绑到对象的生死上,`file_lock` 的骨架如下,连同 demo 的完整版收在 05-raii-file-lock/ 的目录里:

```cpp
// file_lock.hpp(节选):构造加锁,析构放锁,move-only
class file_lock
{
public:
    explicit file_lock(const char* path, bool exclusive = true)
        : fd_{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)}
    {
        lock(exclusive);
    }

    file_lock(const char* path, defer_lock_t)     // 只 open 不上锁
        : fd_{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)}
    {
    }

    ~file_lock()
    {
        if (fd_) {
            ::flock(fd_.get(), LOCK_UN);   // 显式放锁;紧随的 close 是第二道保险
        }
    }

    file_lock(file_lock&& other) noexcept             // fd 所有权移交
        : fd_{other.fd_.release()}, exclusive_{other.exclusive_}
    {
    }
    file_lock& operator=(file_lock&& other) noexcept; // 同思路,见存档
    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;

    bool try_lock(bool exclusive = true) noexcept
    {
        int r = ::flock(fd_.get(), (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB);
        if (r == 0) { exclusive_ = exclusive; return true; }
        if (errno == EWOULDBLOCK) { return false; }   // 冲突不算错误
        return false;   // 其余 errno 理应抛,noexcept 里只能吞,注释记着这个取舍
    }

    template <class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period> d, bool exclusive = true)
    {
        const auto deadline = std::chrono::steady_clock::now() + d;
        for (;;) {
            if (try_lock(exclusive)) { return true; }
            if (std::chrono::steady_clock::now() >= deadline) { return false; }
            ::usleep(1000);            // 1 ms 轮询:分辨率即轮询间隔
        }
    }

private:
    unique_fd fd_;
    bool exclusive_ = true;
};
```

咱们要交代的设计有三处。析构里的显式 `LOCK_UN` 其实是多余的——close 本来就会放掉 flock 锁——咱们留着它,图的是把“放锁”写在代码里看得见的位置,真出了事排查的时候,时间线上有一句明确的放锁可对。`try_lock` 的口径是:EWOULDBLOCK 是“锁被占着”这个正常的答案,它返回的是 false,异常是不抛的。别的 errno 理应升级成异常,偏偏 `noexcept` 的屋檐下抛就是 terminate,头文件的注释里如实记着这个妥协,真实项目可以放宽成可抛的接口。`try_lock_for` 是最无奈的一个:flock 没有“等多久”的原生参数,阻塞版是一等到底的,`F_SETLKW` 也是一样的,所以只能 `LOCK_NB` 加 1 毫秒的小步轮询,等待的分辨率就是轮询的间隔。咱们在 E1a 见过的“放锁瞬间接手”的贴身时序,在这儿要付最多 1 毫秒的迟延,想要更精细的等待,咱们就得拆成两段加信号量去搭,那已经是另一篇文章的活了。

双进程的时序咱们跑给您看:A 构造完就持了锁,写入了 ticket=42,锁一握就是 700 毫秒的工夫。B 拿 `defer_lock` 的姿势起手,把三种姿势各试了一遍:

```text
$ ./e5
pid=169508, 文件:/home/charliechen/l05_scratch/e5/raii.bin(ext4)

==== 双进程时序:A 拿写锁写数据,B 有限等待接棒 ====
  [     0 ms] A(pid=169508):构造 file_lock,已写 "ticket=42",持锁 700 ms
  [     0 ms] B(pid=169509):try_lock() = false(锁在 A 手里)
  [   201 ms] B:try_lock_for(200ms) = false(实际等了 200 ms,超时)
  [   700 ms] A:作用域将尽,file_lock 析构在即
  [   700 ms] A:已放锁
  [   700 ms] B:try_lock_for(3s) = true(等了 499 ms —— A 一放锁就拿到)
  [   700 ms] B:读到 "ticket=42"(临界区数据完好)

==== move 语义:锁跟着新主人走,moved-from 析构不放锁 ====
  [   701 ms] 探针(lk1 持锁中):flock = -1(锁被占)
  [   701 ms] move 后:lk1 还活着吗?否(moved-from);lk2 持有 fd=3
  [   701 ms] 探针(lk1 析构后):flock = -1(锁被占)
  [   702 ms] 探针(lk2 reset 后):flock = 0(拿到锁)
```

时序的那半边,B 的三连正好把三种等待姿态排开:立刻问一次,答了 false。限定 200 毫秒的那次,如期超时了。限定 3 秒的那次,在 499 毫秒处(A 放锁的瞬间)拿到了,还读到了 A 写进临界区的数据。move 的那半边,moved-from 的 lk1 析构时探针依旧被挡——它已经交出了 fd,析构碰不到新主人的锁。等 lk2 显式 reset 了,探针才拿到了。移动构造把对方的 fd `release()` 过户,moved-from 从此成了空壳,您看输出里“探针(lk1 析构后)仍被挡”的那一行,验的就是它了。整套骨架与 `unique_lock` 的心智模型同构,咱们就不多解释了。

## E6 争用实测:八个进程排成一条队

锁是好用的,价咱们也得认。E6 的口径:咱们 fork 出 8 个子进程,让每个子进程做独立的 open,咱们用管道对齐起跑线,每轮都是拿锁、睡 10 毫秒(模拟临界区)、放锁的三步,这样的一轮共 100 趟。对照组是不拿锁照睡的。咱们再来一组空临界区,8 个进程各打 2000 轮的纯 lock/unlock。三种模式咱们各跑三轮,取中间那一轮的数:

```text
$ ./e6
pid=169910, 文件:/home/charliechen/l05_scratch/e6/contention.bin(ext4), 8 工作进程 × 100 轮,临界区 usleep(10ms),3 轮取中位

模式        r1            r2            r3
locked      8090.4 ms    8090.5 ms    8091.1 ms  | 中位   8090.5 ms,10113.1 µs/人次
free        1009.8 ms    1009.9 ms    1009.9 ms  | 中位   1009.9 ms,1262.4 µs/人次

空临界区(纯锁传递开销):8 进程 × 2000 轮 lock/unlock
micro        252.3 ms     255.7 ms     270.0 ms  | 中位    255.7 ms
```

locked 的 8090.5,咱们把算术摆出来:8 进程 × 100 轮 × 10 毫秒 = 8000 毫秒,这是八个临界区完全串行后的理论总时长,实测只多出了 90 毫秒,而且三轮之间的漂移不超过 0.7 毫秒——被串行化的路径反而极稳,因为它根本不吃调度抖动的亏。free 的 1009.9 是同一批进程不拿锁的成绩:8 份 10 毫秒 × 100 轮完全并行,总时长就等于一份的时长。咱们把两个数一比,8 倍的差距量的并非 flock 慢,量的是互斥本身:睡眠被排成了一条队,谁也省不掉的。多出的 90 毫秒摊到 800 次交接上,每次的价钱约 113 微秒,这笔钱里有一趟把睡满 10 毫秒的进程唤醒过来的路程。而 micro 那组没人睡觉,16000 次的交接共 255.7 毫秒,摊出来的价约 16 微秒一次,这才是纯交接的底价。06-contention-bench 目录的 README 还记了一趟旁证:同一台机器上,单个 fd 上无争抢地连打 1000000 对 lock/unlock,0.675 微秒一对——同样的调用,8 路争抢把单价放大了约 24 倍,贵出来的部分全是睡眠与唤醒的路程,锁表本身的操作在 0.675 微秒那一档里早就量过了。

这组数字换算成工程判断是这样的:临界区 10 毫秒的时候,锁的杂费占总时长约 1%,咱们怎么写都不亏。临界区趋近于零的时候,16 微秒一次的地板就露出来了,8 个进程疯狂抢一把空锁的时候,吞吐是会卡在交接费上的。所以用法上,咱们把真正要保护的整段读写放进临界区,让每次 16 微秒的交接护住足够大的工作量,别拿它当高频的小锁使。

## E7 tmpfs 对照:锁的实现都在 VFS 层

最后一个疑问也一并了结:这些语义是 ext4 给的,还是内核统一给的?咱们把同一份探针(fs_probe.cpp,四组小观察)分别对着 ext4 与 tmpfs 跑了两趟,本机的 /tmp 挂的就是 tmpfs(以 df -T 为准)。两份输出咱们并排放,左边的是 ext4,右边的是 tmpfs:

```text
$ /tmp/e7 ~/l05_scratch/e7/ext4.bin                    $ /tmp/e7 /tmp/l05_e7_tmpfs.bin
文件:/home/charliechen/l05_scratch/e7/ext4.bin         文件:/tmp/l05_e7_tmpfs.bin
st_dev=8:48(十进制)= 08:30(十六进制),inode=1716962    st_dev=0:77(十进制)= 00:4d(十六进制),inode=11972

[P1] flock 互斥:A 持锁 300 ms,B 阻塞版接棒           [P1] flock 互斥:A 持锁 300 ms,B 阻塞版接棒
  B(pid=170777):阻塞 300 ms 后拿到锁                   B(pid=170788):阻塞 300 ms 后拿到锁

[P2] 同进程重新 open:flock(LOCK_EX|LOCK_NB)           [P2] 同进程重新 open:flock(LOCK_EX|LOCK_NB)
  fd2 试锁 = -1, errno=11(EWOULDBLOCK)                 fd2 试锁 = -1, errno=11(EWOULDBLOCK)

[P3] fcntl:close 无关 fd → 记录锁全释放?              [P3] fcntl:close 无关 fd → 记录锁全释放?
  竞争者:F_SETLK = -1(被占)                           竞争者:F_SETLK = -1(被占)
  已 close 无关的 fd2                                   已 close 无关的 fd2
  竞争者:F_SETLK = 0(拿到)                             竞争者:F_SETLK = 0(拿到)

[P4] 持 flock 时 /proc/locks:                         [P4] 持 flock 时 /proc/locks:
  /proc/locks: 13: FLOCK  ADVISORY  WRITE 170776 08:30:1716962 0 EOF   /proc/locks: 6: FLOCK  ADVISORY  WRITE 170787 00:4d:11972 0 EOF
```

咱们把路径、pid 和行首序号归一化之后再看,两组输出是逐行一致的:P1 的互斥时序、P2 的自冲突、P3 那个全释放的陷阱,tmpfs 一样不少地重演了一遍。道理其实不深:flock 与 fcntl 记录锁都实现在 **VFS**(virtual file system,Linux 给各文件系统统一接口的那一层)里,ext4 与 tmpfs 都只是挂在它下面的具体文件系统,连 /proc/locks 都照常看得见 tmpfs 的锁。两份输出唯一的差异在 P4 的设备号字段:ext4 那行是 08:30(/dev/sdd 的块设备号),tmpfs 那行是 00:4d——tmpfs 是没有块设备的,那是内核配给它的匿名设备号,顺带把 inode 的数量级也拉小了一截。

您现在回头看本篇开头的那个问题:A 写到一半,B 进来了怎么办?两套锁手里都有答案。flock 是简单粗壮的,拿整个文件当临界区的用法,配合 `O_CREAT|O_EXCL` 抢出来的锁文件,就是单实例守护进程的经典写法。fcntl 的记录锁能按字节分片,却带着 E2d 那个 close 全释放的陷阱,新代码改用了 `F_OFD_SETLK`,这个陷阱就没有了。咱们挑哪一套,看的还是开头那一句话:锁属于谁。推不动的时候,您回来翻 E1 到 E4 的输出,存档里躺着现成的答案。

## 另一侧怎么看

Windows 的手里没有 flock,也没有 fcntl 式的记录锁,同一件事它交给了 `LockFileEx` 与 `UnlockFileEx`:上锁的单位是字节区间,独占还是共享的选取由 `LOCKFILE_EXCLUSIVE_LOCK` 决定,想要非阻塞的话就得加 `LOCKFILE_FAIL_IMMEDIATELY`,语义的轮廓跟 fcntl 记录锁更接近。还有一层是 Linux 这边没有的:`CreateFileW` 的 `dwShareMode` 在打开那一刻就声明了共享限制,别的句柄想以冲突方式打开同一个文件,直接在 open 的门口就被拒绝了。两边的对照(包括 LockFileEx 的等待语义与句柄继承),咱们留到 Windows 侧讲文件锁的镜像篇([文件锁:LockFileEx](../../windows/file-io/05-lockfileex.md))再对读,地基您看 [Win32 文件 I/O](../../windows/file-io/01-win32-file-io.md)。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="flock(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/flock.2.html"
  />
  <ReferenceItem
    :id="2"
    title="fcntl_locking(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl_locking.2.html"
  />
  <ReferenceItem
    :id="3"
    title="F_OFD_SETLK(2const)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/F_OFD_SETLK.2const.html"
  />
  <ReferenceItem
    :id="4"
    title="fcntl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl.2.html"
  />
  <ReferenceItem
    :id="5"
    title="proc_locks(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_locks.5.html"
  />
  <ReferenceItem
    :id="6"
    title="open(2) —— open file description 的定义处"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/open.2.html"
  />
  <ReferenceItem
    :id="7"
    title="nfs(5) —— local_lock 选项"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/nfs.5.html"
  />
  <ReferenceItem
    :id="8"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
