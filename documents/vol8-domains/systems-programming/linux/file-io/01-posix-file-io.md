---
title: "POSIX 文件 I/O:open/read/write 与 fd 的一生"
description: "Linux 侧文件 I/O 的第一块地基:用 strace 亲眼看完一个 fd 从 open 出生、read/write 干活到 close 落幕的一生——flags 家族与 umask 的交割、部分读写为什么是常态、fd 表与 dup2/dup3 重定向、fcntl 两类标志的归属、pread 的不动偏移、SEEK_DATA/SEEK_HOLE 摸清稀疏文件、页缓存与 fsync 的崩溃窗口;unique_fd 与 sys_call 这对公共工具沿用思维基石两篇的定义,本篇带它们打第一场实战"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 32
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
related:
  - "mmap 内存映射:把文件贴进地址空间"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# POSIX 文件 I/O:open/read/write 与 fd 的一生

咱们这一卷讲系统编程,总纲里已经把主线约好了:沿着 Linux 与 Windows 两大阵营并行对照的路线,从应用层的 OS API 一路摸到 Modern C++ 的封装。这一篇是 Linux 侧的开头,起点我们挑了文件 I/O。咱们要写的 `open`、`read`、`write`,名字看上去就是一副人畜无害的样子。可是您再想想日志、配置、数据库,而 socket 也一样,它们在外面兜了一大圈,最后全都得落回同一组调用和同一个小整数的身上。

咱们要天天打交道的这个小整数,其实就是 **fd**(file descriptor)。内核给每个进程都记着一张已打开文件描述符的表,fd 就是表里的一个下标:0/1/2 在进程出生时就被 stdin/stdout/stderr 占了,所以您新开的 fd 从 3 开始。咱们查表以前,fd 不过是个号码,没有对象的样子,也没有指针的样子。接下来咱们就陪着 fd 走完它的一生:从 `open()` 里出生,受 `read`/`write` 的使唤,最后在 `close()` 落了幕。

API 手册咱们暂时不急着背,转身就把 strace 请了出来,拍一份 syscall 层的现场给大家看。下面这个程序干的事,拿 `O_WRONLY|O_CREAT|O_TRUNC` 建了文件写 13 字节,随后把它关掉了,再以 `O_RDONLY` 的只读方式读 64 字节:

```text
$ strace -e trace=openat,read,write,close ./exp1
openat(AT_FDCWD, "/tmp/sysprog-linux01/hello.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(3, "hello, posix\n", 13)          = 13
close(3)                                = 0
openat(AT_FDCWD, "/tmp/sysprog-linux01/hello.txt", O_RDONLY) = 3
read(3, "hello, posix\n", 64)           = 13
+++ exited with 0 +++
```

> 动态链接器打开 `.so` 的那几行 `openat`,咱们省掉没贴。您要是去看完整的 trace,会发现它们个个都带着 `O_CLOEXEC` 的标记。

其实 trace 里只有五个 syscall,里面却藏了两处值得咱们停下来看的细节。

咱们看头一处:明明调的是 `open`,trace 里却只见到 `openat` 的影子,欸?您别慌,这当然不是玄学。如今的 `open()` 是 glibc 里的一层兼容外壳,真正发出去的 syscall 是 `openat(AT_FDCWD, ...)`,AT_FDCWD 的身份是个哨兵值,意思是对相对当前目录的路径做解析,把这层皮留在用户态的是 glibc,而不是内核。到了 glibc 2.26 这一代,glibc 的 `open` 包装就统一改走了 `openat`,man 2 open 里的 C library/kernel differences 一节,把这件事交代清楚了。有意思的是,x86_64 的内核 syscall 表里 `open` 至今健在,而 arm64/riscv64 这类新架构,才是真的没有了它。

另一处请您看最后一行:`read(3, ..., 64) = 13`,咱们明明申请了 64 字节,结果只拿到 13。其实整个文件也就 13 字节,读到了文件尾,它自然就收了工、而不是报了错。部分读写是 POSIX 的常态,咱们后面专门展开。

边界也顺手交代一下:C 的 `fopen`/`fread` 卷一讲过([C 文件 I/O 与 stdlib](../../../../vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md)),`std::filesystem` 咱们留给本卷后续讲目录与元数据的篇章,mmap 是下一篇([把文件贴进地址空间](./02-mmap-memory-mapping.md))的主角,而文件锁和 inotify,咱们放到本卷后续章节再说。错误处理的这边,本文用的类型底座还是 `std::error_code` 和 `std::system_error`,体系的来龙去脉,vol3 的 [error_code 深入](../../../../vol3-standard-library/error-utils/66-error-code.md) 已经完整讲过,咱们不在这里再讲一遍。而 Linux 侧怎么把它们用得顺手,思维基石的两篇早就备好了答案:`unique_fd` 的定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md),`errno_code` 与 `sys_call` 的定义在[错误处理范式](../../thinking/02-error-paradigm.md),咱们本篇直接领来用,三件工具的定义一个都不重写。

实验的口径也交代在开头,省得您后面对不上号:本篇的实验按 exp1 到 exp11 编号,编号跟出场的顺序不总是一致,exp5 就排在 exp4 的前面,后文您还会见到这样的。从 exp9 到 exp11 的三个,也就是 fcntl 的标志、dup 一家加 close-on-exec、lseek 的 SEEK 家族,代码与原始输出都入了册,收在仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/01-posix-file-io-supplement/` 下面的 01-fcntl、02-dup、03-seek 三个目录,复现命令写在各目录的 README 里,编译一律 g++ 16.2.1 的 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,拿到的警告数是零。存档内部用的是它自己的标号:三个实验在各自的源码与 README 里记作补课段 E1、E2、E3,产物与数据目录走的短名是 e1、e2、e3,咱们正文里的 exp9、exp10、exp11,对过去就是存档的 E1、E2、E3。姊妹篇的编号也请您别拿错尺子:[L02](./02-mmap-memory-mapping.md) 用的是小写 e1 到 e5,L03 往后各篇用的是大写 E,它们与咱们存档里的 E1、E2、E3 各算各的、谁也不挨着谁。

## open():flags 全解,以及 umask 砍权限的那一下

`open()` 收三个参数:路径、一坨按位或起来的 flags,外加排在第三的 mode。mode 只在带 `O_CREAT` 的时候才有意义。flags 的最低两位是访问模式,咱们三选一:`O_RDONLY`(0)、`O_WRONLY`(1)、`O_RDWR`(2)。它们的取值其实就是 0/1/2 本身,所以您可别耍小聪明写 `O_RDONLY|O_WRONLY`,按位或出来的结果还是 `O_WRONLY`,内核就只会理解成了只写打开。而在访问模式之外,常用的就是这几个:

- `O_CREAT`:不存在则创建,mode 参与定权限(umask 怎么砍它,咱们马上讲)
- `O_EXCL`:与 `O_CREAT` 连用,存在即失败。不存在则创建、存在则报错,这个判断在内核里是一次性的、原子的,锁文件的安全创建全靠它,还能替咱们挡住符号链接偷袭
- `O_TRUNC`:打开时把普通文件清零。前提是访问模式允许写,您拿只读 fd 打开,就别惦记它了
- `O_APPEND`:每次 write 前,内核把偏移原子地挪到文件尾,多进程追加日志的正解,咱们到 fsync 一节再展开
- `O_CLOEXEC`:exec 成功的瞬间自动关闭这个 fd,咱们直接看下面的 warning 块
- `O_DIRECT`:绕过页缓存直写,带一堆对齐要求,本篇不展开,咱们知道存在即可

::: warning O_CLOEXEC:新代码默认带上
不带 `O_CLOEXEC` 的 fd,会被 `fork()` 出来的子进程原样继承。而在多线程程序里,这一点是重灾区:您刚 `open` 完,还没来得及用 `fcntl` 补设 `FD_CLOEXEC` 的空档里,另一个线程恰好完成了 `fork`+`exec`,新程序就揣着一个您不认识的 fd 启动了,这可就是经典的 fd 泄漏了。`O_CLOEXEC` 让内核在 exec 那一刻替咱们关掉它,竞态窗口也就直接归了零。笔者的习惯是库代码一律带上,本文的示例用完就 close,偶尔也省略了。
:::

mode 还经常给人惊喜:明明传进去的是 0666,`ls -l` 里看到的却是 644,差在哪儿了?咱们实测了一把(环境用的 WSL,g++ 用的是 16.2.1,内核跑的是 6.18)。程序一上来就调了 `umask(022)`,接着就用 mode 0666 建了文件,再用 `fstat` 读回真实的权限,随后咱们对同一个文件,补一次 `O_CREAT|O_EXCL` 的二次打开:

```text
$ ./exp2
umask=022, request=0666, real=0644
second open with O_EXCL: fd=-1, errno=17 (File exists)
```

为什么?man 2 open 写得直白:带 `O_CREAT` 的时候,最终权限的算法就是 **mode & ~umask**。umask 是进程的一种属性,shell 里 `umask` 命令设下的 022,把组写和其他写两位砍掉了,0666 就这么成了 0644。您可以把它理解成一道安全闸:哪怕程序手滑传了 0666,进程也没资格造出人人可写的文件。您真想要 0666,就得在您的程序里自己动手 `umask(0)`,不过这一步,请您想清楚了再动手。至于第二行的输出,errno 17 对应的正是 `EEXIST`。咱们这把测的,是单个进程对同一个文件的二次打开:文件还好好地躺在那里,`O_CREAT|O_EXCL` 当场就把它挡了回来。换到两个进程抢建同一个锁文件的场景,赢的只有一个,输家拿到的也正是这个 `EEXIST`。检查不存在加创建的这件事,在内核里是一次性的原子操作、谁也插不进队去。

## read/write:部分读写才是常态

咱们刚才在 strace 里看到的那一次,要的是 64,拿到的只有 13,这不是偶然的。man 2 read 的原文写得很干脆:**读到的字节比请求的少,这其实不是错误**。这也许是因为接近了文件尾,也可能是因为对面的管道、终端,数据暂时也就这么多了。我们把一个 100 字节的文件,交给 16 字节的小缓冲循环 `read`,每轮的返回值打一行(round 0~4 五轮全是 `got 16`,咱们略过):

```text
$ ./exp3
round 5: want 16, got 16
round 6: want 16, got 4
round 7: want 16, got 0
```

round 6 的请求是 16,可读的只剩 4。round 7 返回的 0 就是 EOF,流到头了。`write` 也是同样的道理:返回值是实际写入的数,有可能比请求的少。落到咱们手里的,其实只有一条守则:read/write 的调用必须循环,一次调用成功了,不代表事情做完了,后面的 `write_all`,就是把这个循环封装起来的产物。另外提醒您一句,您别急着把这些 fd 塞给 epoll:普通文件进不了 epoll,`epoll_ctl` 会直接给您 `EPERM`,原话就收在参考资源里 man 2 epoll_ctl 的条目里。普通文件永远是就绪的,多路复用本来就是 socket 与管道的世界。epoll 机制本身是怎么运转的,您可以到 [网络卷的 epoll 一篇](../../../networking/02-epoll-io-multiplexing.md) 去看。

## 错误处理:三件公共工具,直接领来用

syscall 的调用一旦失败,返回的就是 -1,细节全留在了 errno 里。既然每次调用都有失败的可能、失败了就要看 errno,这套活儿咱们系列里已经收进了三件公共工具:`errno_code` 与 `sys_call` 的定义在[错误处理范式](../../thinking/02-error-paradigm.md),`unique_fd` 的定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md),咱们认一次定义就够,本篇起把它们当现成的词汇直接用。

咱们用一句话把用法唤醒:`sys_call("open", ::open, path, flags, mode)` 失败的时候抛 `std::system_error`,errno 装进了 `generic_category`(POSIX errno 恰好就是 `std::errc` 的值域),`what` 则成为异常消息的前缀,咱们 catch 到手,一眼就知道是哪一步炸的。还要请您留意一处:read 返回 0(EOF)并不是 -1,`sys_call` 的处理是原样放行、把 EOF 留给调用方去判断,这是刻意的设计。

不走异常的场合,同一套装箱还有非抛的形态,咱们照样在失败分支的头一行动手:

```cpp
// 非抛路径的用法示意:裸调用的失败分支里,errno 就地装箱,不惊动异常
int fd = ::open(path, O_RDONLY);
if (fd == -1) {
    std::error_code ec = errno_code();   // 此刻定格,后面再发生什么都不影响 ec
}
```

这就是 `errno_code` 替咱们派上用场的地方。`sys_call` 抛出的 system_error,咱们 catch 到手,它身上携带的 error_code 也是同一套装箱,而工具层想拿 `std::expected` 传错的场合,交到链上的还是它。

还有一位要专门点到的:EINTR。您想,咱们在阻塞的 read 上等数据,信号来了,内核把咱们的进程叫醒去跑处理函数,read 没法子继续等了,只好把 -1 交了回来,errno 里躺着的就成了 EINTR。设备其实没有坏,咱们碰上了它,把调用从头再来一次就成了。`sys_call` 的完整版把这次重试做在了内部:errno 是 EINTR 就重试一轮,其余的失败才装箱抛出。想看它跟 SA_RESTART 的内核重启怎么对质,strace 里的证据链长什么样,[错误处理范式](../../thinking/02-error-paradigm.md) 的 E2 实验演了完整的一场,咱们不在这里重讲。您只认一条:经 `sys_call` 过手的调用,被信号打断的事情,不用您再操心。

## unique_fd:把 close 写进析构函数

裸 `int fd` 的毛病,写过程序的您多半领教过:函数里写了三条提前 return,`close` 写在了第四条路径上,而不泄漏的保障,全靠人肉的 review。解法您其实已经拿到手了:[OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md) 里那个 move-only 的 `unique_fd`,把 `close` 写进了析构函数,漏不漏的问题,从此就不靠记性了,靠的是类型。类的完整体、拷贝为什么整个删掉、release/reset/swap 的归还语义,您在那一篇里都逐行看过,咱们这里不再抄一遍代码,只把一处容易念错的细节再对一次表。

`explicit operator bool` 的那个 explicit,挡的东西跟很多人以为的正好相反。`if (fd)` 这样的条件语境,它是根本不挡的:语境转换本来就给 explicit 留了门,您写 `if (fd)`,编得好好的。它真正拦下的,是把对象当值用的隐式转换,`bool ok = fd` 这一句 GCC 当场回您 cannot convert to bool in initialization,连 `fd == 0` 也一起编不过了。要是咱们去掉这个 explicit,这两句就都悄悄编过了。报错的原文,RAII 篇替咱们实测过,您翻回去就能对上。

unique_fd 防的是什么事故,光说不过瘾,咱们故意漏一次给您看:程序开头遍历 `/proc/self/fd` 打印现有的 fd,再用 `setrlimit` 把 RLIMIT_NOFILE 压到了 8,然后咱们循环 `open` 同一个文件,偏偏故意不 close:

```text
$ ./exp5
fds at start: 0 1 2 3 5 10
leaked fd 3
leaked fd 4
leaked fd 6
leaked fd 7
open() failed after 4 leaked fds: errno=24 (Too many open files)
```

这几行输出的信息量不小,咱们一行行看。开场的列表里,3 是 `opendir("/proc/self/fd")` 自己占的槽位,列完 `closedir` 就放掉了,于是头一个泄漏又拿回了 3,fd 的分配永远是取最低的空闲位。5 和 10 则是 WSL 启动链塞给进程的继承 fd,根本不是我们开的,`/proc/self/fd` 也照单全收了。所以您的 fd 表里,本来就可能有别人塞进来的东西。往后排查 fd 的问题,咱们从这个目录看起。

接着 4、6、7 一路漏了过去(5 号被占了,跳过了),第 5 个 open 顶上了 RLIMIT_NOFILE=8。errno 24 对应的就是 `EMFILE`。真实环境的额度,您看 `ulimit -n` 就知道,笔者的 WSL 实测是 1048576(2026-10-02 量的,RAII 篇实验里 getrlimit 读到的也是同一个数),老发行版里常见的是 1024。长跑的服务每接一个请求就漏一个 fd,额度就是这么一点点吃光的。而泄漏掉的 fd,要等到进程退出了才释放,中途是没有谁会还的。

## dup/dup2/dup3:shell 的重定向是这么来的

咱们想把 fd 表看明白,就得知道它的两级结构:一级是进程私有的 fd 表,每一项都指向系统级的**打开文件描述**(open file description),偏移量和状态标志都存在后者的身上。`open` 了两次,得到的是两个独立的 description,各写各的偏移。`dup` 和 `fork` 复制的是 fd 表项,于是两个 fd 就指向了同一个 description,共享了偏移。fork 了之后,父子进程接着对方的偏移往下写,而奥妙就在这里,细节咱们留到进程篇再展开。

`dup2(fd, 1)` 干的事更直接,咱们看代码:它把 fd 表的 1 号槽位,原子地换成指向 fd 的文件,此后一切 `write(1, ...)`、`printf` 就全进了文件。shell 的 `> file`,正是 fork 完了、exec 还没跑的那一下(exp4_dup2.cpp 的节选,咱们省去 include 与 main 的外壳):

```cpp
int fd = open("/tmp/sysprog-linux01/out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
int saved_stdout = dup(1);     // 留一条回终端的路
dup2(fd, 1);                   // 1 号槽位原子地换成文件
std::printf("this line goes into out.txt\n");
std::fflush(stdout);
dup2(saved_stdout, 1);         // 换回终端
```

```text
$ ./exp4
back to terminal
$ cat /tmp/sysprog-linux01/out.txt
this line goes into out.txt
```

咱们另外排了一个 exp10,把 dup 一家子挨个过了一遍,顺手还抓到了两个旁观的发现。一个发现出在 stdio 缓冲的落点上:重定向期间,咱们 printf 了一行却没 fflush,它就一直躺在用户态的缓冲区里,直到 dup2 把 1 号换回了终端,才被后面的输出带着冲了出来,落点已经是终端了。所以您看,flush 的落点跟着 flush 那一刻的 fd 1 走,不跟 printf 的调用时刻走。另一个发现关乎原子性的价钱:您要是 close(1) 再 dup,close 与 dup 中间的空窗里,进程里的任何一次 open(多线程程序里完全可能是别的线程干的)都会把 1 号槽抢走,实测里下一次 open 拿到的正是 1,咱们想恢复就得再 close 再 dup,一来一回之间全是竞态的窗口。dup2 把两步并成了一条系统调用,窗口也就关上了。

再补一个容易漏的冷知识:dup 出来的新 fd **不带** `FD_CLOEXEC`,哪怕原来的 fd 带了。man 2 dup 的原文写得很明确,close-on-exec 是不共享的。您要带着它过 exec 边界,这个标志位就得重新检查一遍了。

补救的路子有两条。一条是复制完了再补,咱们用 fcntl(2) 的 F_SETFD 命令,给新的 fd 把 FD_CLOEXEC 重新设上,它是咱们自己顺出来的补充。另一条是 man 2 dup 明明白白指给咱们的 dup3(2),它是 Linux 起家的,如今已经进了 POSIX.1-2024,咱们让 exp10 的 e 段把兄弟俩摆在一起:

```text
$ ./exp10(节选:e 段)
dup2(fd,10)  F_GETFD=0  dup3(fd,11,O_CLOEXEC) F_GETFD=1
dup2(fd,fd)  = 3(检查后发现是自己,直接返回,no-op)
dup3(fd,fd,0)= -1, errno=22(EINVAL)  <- dup3 明确拒绝
```

dup3 与 dup2 的差异,咱们就记两处。头一处是 flags 参数里带上了 `O_CLOEXEC`,复制件一出生就自带了它,您看第一行里 11 号的 F_GETFD=1,就是它设的。另一处容易让人意外的地方,在 oldfd 与 newfd 相同的场合,dup2 检查出来是自己就无害地返回了事,dup3 则直接回您一个 EINVAL,告诉您这么写不行。

接下来是 exp10 的正题:close-on-exec 的生死,咱们用 fork 加 exec 亲手验一遍。三个 fd 咱们一次备齐,3 号是裸 open 出来、什么标志都没带的 fd。4 号也是裸 open 出来的,咱们随后用 F_SETFD 给它补上了 FD_CLOEXEC。20 号是从 4 号复制出来的,用的 F_DUPFD,这是 fcntl 的复制命令,跟 dup 是等价的,还能指定复制的下限,20 号就是冲着下限来的。随后咱们 fork 出子进程,子进程把 `/proc/self/fd` 列了一遍,再用 execl 把自己重新 exec 了,三个 fd 号走 argv 传给了新程序:

```text
$ ./exp10(节选:f 段)
[child exec 前] /proc/self/fd: 0 1 2 3 4 20(pid=320407)

[child exec 后] /proc/self/fd: 0 1 2 3 20(pid=320407)
(exec 前是 0 1 2 3 4 20;对照上面,4 没了、3 和 20 还在)
read(3, ...)          = 10:"0123456789"  <- 裸 fd 活过了 execve
fcntl(4, F_GETFD)     = -1, errno=9(EBADF)  <- 带 CLOEXEC 的 fd,exec 前被内核关了
read(20, ...)          = 10:"0123456789"  <- 它复制自带 CLOEXEC 的 4,但 fd 标志不随 dup 走,又活了
```

同一批 fd 号有了三种命运,咱们挨个看。3 号是裸的,平平常常活过了 execve,exec 之后照样读出了 10 个字节。4 号带着的是 FD_CLOEXEC,exec 之后咱们拿 F_GETFD 一查,拿到的是 EBADF,它在新程序里已经不存在了。最有意思的是 20 号:它明明复制自带 CLOEXEC 的 4 号,却活得好好的。为什么?dup 一节的冷知识正好解释它:fd 标志住在 fd 表项上,复制表项的过程里 FD_CLOEXEC 是不跟着走的。

那 4 号是被谁关掉的?咱们把 strace 请出来看全程,看到的跟您想的可能不一样:

```text
320037 fcntl(4, F_SETFD, FD_CLOEXEC)    = 0
320037 fcntl(4, F_DUPFD, 20)            = 20
320038 execve("/proc/self/exe", ["e2_dup(child)", "--child", "3", "4", "20"], 0x7ffe9768ea08 /* 72 vars */) = 0
320038 openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 4
320038 close(4)                         = 0
```

从 fcntl 到 execve 的这段里,咱们找不到一次 close(4)。关闭不是一次独立的系统调用,它做在 execve 自己的内核路径里:exec 换新程序映像之前,内核把 fd 表全扫了,带 FD_CLOEXEC 的表项就地清掉。紧跟其后的两行,还有个顺手的观察:动态链接器开工的头一件事就是 openat ld.so.cache,它拿到的正是刚空出来的 4 号槽,用完就随手关掉了。所以 exec 之后您看到一个 4 号,别急着认定它活过了 exec,它可能是新程序自己开的。想验一个 fd 的生死,咱们别赌 fd 号,咱们像上面那样 read 一下、F_GETFD 一下,行为给的答案才算数。对了,trace 里 execve 的 argv[0] 写着 e2_dup(child),那就是存档给 exp10 起的二进制名,套的还是存档自己的 e2 短名,您可别把它认成 L02 的小写 e 系。

## fcntl:两类标志,住在两层

上一节咱们反复用到 F_SETFD 和 F_DUPFD,现在咱们把 fcntl(2) 这个接口本身看清楚。它是个按命令字干活的杂务接口,咱们把 fd 交给它,再配一个 F_ 开头的命令字,往后的参数就跟着命令走。命令的数量虽多,眼下咱们认最常用的两类也就够用,而两类的区分,正好落在 fd 的两级结构上。

一类管的是 **fd 标志**,它的全部就一位:FD_CLOEXEC,住在进程私有的 fd 表项里,咱们读写它,用的命令是 F_GETFD/F_SETFD。另一类管的是**文件状态标志**,O_APPEND、O_NONBLOCK、O_ASYNC 这些都算它的,住在系统级的打开文件描述上,命令换成了 F_GETFL/F_SETFL。标志住的位置决定了复制的时候跟不跟着走,咱们让 exp9 的 h 段拿同一个 fd 和它的复制件对了一遍:

```text
$ ./exp9(节选:h 段)
            f(原件)   g(F_DUPFD 复制件)
O_NONBLOCK  有      有      <- 状态标志在描述上,两个 fd 共享
FD_CLOEXEC  有      无      <- fd 标志在表项上,复制件不带走
```

dup 一节的冷知识,对照表一摆就有了机制上的说法:dup 与 F_DUPFD 复制的是 fd 表项,新表项上 FD_CLOEXEC 总是清零的,而新旧两个表项又指向同一个打开文件描述,所以状态标志两边共享,而 close-on-exec 只留在原件上。往远处再说一句:flock 的锁挂在打开文件描述上,fcntl 的记录锁挂在进程上,这些归属差异也都是从两级结构推出来的,咱们到 [文件锁](./05-file-lock.md) 那篇再展开。

咱们再看复制这一侧,fcntl 自己也备了复制的命令,exp9 的 b 段把三条路摆在了一起给咱们看:

```text
$ ./exp9(节选:b 段)
dup(fd=3)               = 4   (最低空闲)
fcntl(fd, F_DUPFD, 20)       = 20   (>=20 的最低空闲)
fcntl(fd, F_DUPFD_CLOEXEC,20)= 21   (一步到位)
```

dup 拿的永远是最低的空闲位,F_DUPFD 让咱们指定下限,exp10 里的那个 20 号,走的就是它,而 F_DUPFD_CLOEXEC 把 CLOEXEC 也一并带走,复制加补标志的活儿,咱们一条调用就办完了。

F_GETFL 的返回值里还埋着一个固定的偏差,咱们看 exp9 的 c 段:

```text
$ ./exp9(节选:c 段)
F_GETFL(O_RDWR open)   raw=0100002  accmode=O_RDWR
F_SETFL 加 O_APPEND 后 raw=0102002  accmode=O_RDWR |O_APPEND
```

raw 是 F_GETFL 原样返回的八进制。您要是拿它跟传给 open 的 flags 判相等,那永远是错的:raw 里头固定多了一位 0100000,那是内核带回来的 O_LARGEFILE 位,内核拿它标记 64 位 off_t 的打开方式。glibc 侧的宏名叫 __O_LARGEFILE,64 位平台上它被定义成了 0,于是用户态的 O_LARGEFILE 名存实空,所以内核的回答会多出一位,而它从不出现在咱们的入参里。所以判访问模式,咱们别嫌麻烦:咱们用 `fl & O_ACCMODE` 把访问模式抠出来再比,标志位的判断则一律按位与。

F_SETFL 的脾气更要记牢:它做的是**整体覆盖**而非按位或。您想加 O_NONBLOCK,要是您直接 `F_SETFL(fd, O_NONBLOCK)` 裸设过去,原有的 O_APPEND 就被冲掉了,exp9 的 d 段专门造了一次事故给咱们看:

```text
$ ./exp9(节选:d 段)
F_SETFL(O_NONBLOCK) 后 raw=0104002  accmode=O_RDWR |O_NONBLOCK
O_APPEND 没了——正确姿势是 F_GETFL 读出来、按位或、再 F_SETFL:
补救后              raw=0106002  accmode=O_RDWR |O_APPEND |O_NONBLOCK
```

正确姿势就写在输出的第二行里:咱们把 F_GETFL 读出来,按位或上新的标志,再用 F_SETFL 写了回去。还有一条边界要请您知道:访问模式,F_SETFL 是改不动的,man 2 fcntl 写明了,open 之后访问模式就定死了,F_SETFL 认的只有状态位。有意思的地方在这儿:您往只读 fd 上塞 O_RDWR,调用居然给咱们返回了 0,一声不吭地成功,可 write 照样吃了 EBADF:

```text
$ ./exp9(节选:e 段)
fcntl(rd, F_SETFL, O_RDWR|O_NONBLOCK) = 0(居然成功)
再看 F_GETFL         raw=0104000  accmode=O_RDONLY |O_NONBLOCK
write(rd,...) = -1, errno=9(EBADF)  <- 访问模式纹丝不动,白塞
```

那 F_SETFL 平时什么时候用?它是 open 之后改 O_APPEND、O_NONBLOCK 的唯一途径,而不少场合下,咱们恰恰不能重新 open:重新 open 得到的是一个新的打开文件描述,偏移是从头算起的,跟原来的 fd 互不相认。exp9 的 f 段把两条路摆在一起给咱们看:

```text
$ ./exp9(节选:f 段)
fd   当前偏移 lseek(fd,0,SEEK_CUR)   = 10
fd2  重新 open 的偏移 = 0  <- 各是各的描述,和 fd 的 10 互不相认
     (fd2 带着 O_APPEND 出生,lseek 看到的偏移要到 write 那刻才被内核挪到文件尾)
fd   F_SETFL 摘掉 O_APPEND 后偏移      = 10  <- 还是那个描述,偏移没动
```

咱们在同一个描述上动标志,偏移是原地不动的,这就是改标志别重开的原因。O_NONBLOCK 的实效,exp9 的 g 段拿一条 FIFO 当场验了。FIFO 指的是命名管道,咱们用 mkfifo 造一个出来,它出生时是阻塞的,read 没有数据就永远地等在那里。等 F_SETFL 加上了 O_NONBLOCK,同一条 read 就立刻返回了 -1,errno 给的是 EAGAIN(Linux 上它与 EWOULDBLOCK 同值),不再等了。socket 编程里那套非阻塞的写法,追到源头就是它了。

> 回头看本文开头的建议:open 的时候直接带上 `O_CLOEXEC`。咱们用 F_SETFD 补标志,活儿是分两步做的,而 open 带上 `O_CLOEXEC` 是出生就有,多线程程序里 fork+exec 的竞态窗口,正是它替咱们关掉的。

## lseek/pread/pwrite:偏移是内核那头的状态

偏移量不在您的代码里,它住在内核的 open file description 里:read/write 的调用推进它,`lseek` 的调用改它。多线程共享一个 fd 的并发读,就等于共享一支会被彼此挪动的隐形游标。`pread`/`pwrite` 则把偏移变成了调用参数,一次调用等价于原子的 lseek+read,而且读完了也不动游标。咱们用内容为 “0123456789” 的文件实测:第一步咱们 `pread(fd, buf, 4, 2)`,从偏移 2 的位置读 4 个字节。第二步用 `lseek(fd, 0, SEEK_CUR)` 查一下 fd 当前的偏移,随后再用普通的 `read(fd, buf, 4)` 做对照:

```text
$ ./exp8
pread(4, off=2) -> "2345", fd offset now 0
read(4)         -> "0123", fd offset now 4
```

`pread` 从偏移 2 那里拿到了 “2345”,fd 的偏移纹丝不动,停在了 0 上。紧接着的普通 `read`,才是从 0 起步的。正是这一点让 pread 成了多线程并发读同一文件时,咱们最省心的姿势,省去了加锁的麻烦,也不会踩到别人的游标。不过 Linux 在这里有个小脾气:在用 `O_APPEND` 打开的 fd 上,`pwrite` 会无视传入的偏移直接追加。man 2 pread 也明说了,这并不符合 POSIX 的要求,而是 Linux 自己的历史行为。

lseek 的 whence 参数里,还藏着专门伺候稀疏文件的一对角色,咱们要用的就是 `SEEK_DATA` 与 `SEEK_HOLE`。whence 说的就是新偏移从哪儿起算,它的取值,就是 SEEK_ 开头的整个家族。稀疏文件(sparse file)说的是这样一类文件,它们的个头很大,而中间大段大段是洞,洞里的字节一个都没写到盘上,咱们去读,读出来的全是零。咱们用最省事的法子造一个:用 ftruncate(把文件直接裁到指定长度的调用)把个头拉到 1 MiB,再挑三个位置 pwrite 少量的数据,中间的部分全不碰。stat 一出来就露馅了,您看 exp11 的 a 段:

```text
$ ./exp11(节选:a 段)
st_size     = 1048576 字节(1 MiB)
st_blocks   = 24 × 512 = 12288 字节  <- 磁盘实占,st_size 只是「账面」
```

st_blocks 是 stat 结构里记磁盘实占的字段,一块是按 512 字节算的。您看,st_size 报的是 1 MiB,磁盘的实占却只有 12 KiB:24 个 512 字节的块,摊到三段数据的头上各是 8 块,8 块乘 512 字节正好各占一个 4 KiB 的文件系统块。SEEK_DATA 找的是从给定位置往后数的下一段数据,SEEK_HOLE 找的是下一段洞。咱们从 0 出发,用 DATA 找到数据段的起点,HOLE 找到它的结尾,直到文件的末尾,整个文件的哪里是洞、哪里是数据,咱们就全画出来了:

```text
$ ./exp11(节选:b 段)
hole  [      0,    4096)  长度    4096
data  [   4096,    8192)  长度    4096
hole  [   8192,   69632)  长度   61440
data  [  69632,   73728)  长度    4096
hole  [  73728, 1040384)  长度  966656
data  [1040384, 1044480)  长度    4096
hole  [1044480, 1048576)  长度    4096(尾部全洞,SEEK_DATA 报 ENXIO)
洞里 pread(2048, 8) = 8 字节: 00 00 00 00 00 00 00 00  <- 零页,内核现造的,不占磁盘
```

输出末尾咱们还往洞里 pread 了 8 个字节,拿回来的全零并不经过盘,它们是内核现造的零页,也不占磁盘的空间。知道了洞的位置,能干什么?cp、tar 这些工具想做洞感知的复制,靠的就是这一对 whence:数据段咱们照样复制过去,而洞就直接 seek 越了过去,复制品不至于把洞填成实打实的零,白白地占了盘。还有两个边界要请您记下。一个在精度上:探测的粒度是文件系统块,咱们这里是 ext4 的 4 KiB,开头那段 pwrite 只写了 8 个字节,报出来的数据段却是整整 4096,小于一个块的洞,在这套探测里是根本不存在的。您自己造演示用的稀疏文件,洞必须留足一个块的大小,不然测不出想测的东西。另一个在失败上:找不到的时候报的是 ENXIO,全洞的文件是一种,出发点落在文件尾上的是另一种,两种情况咱们拿到的都是它:

```text
$ ./exp11(节选:c 段)
全洞文件 lseek(0, SEEK_DATA)      = -1, errno=6(ENXIO)
从文件尾 lseek(1048576, SEEK_DATA)  = -1, errno=6(ENXIO)
```

咱们还有三件小事要一并交代。头一件是偏移的边界:SEEK_CUR 与 SEEK_END 倒是都收负偏移,咱们拿 `lseek(-4, SEEK_END)` 一试,落在了 6,读出来的是 “6789”,咱们再拿 `lseek(-1, SEEK_CUR)` 一试,游标回拨了一格,同一段字节就能重读了。而 SEEK_SET 的结果得是非负数,负的会被直接拒掉。

第二件是越过文件尾的 seek 也合法:`lseek(+100, SEEK_END)` 给咱们返回了 110,此时的 st_size 仍是 10,咱们把位置定到了洞里,而字节还没有写到盘上,文件的长大要等下一次 write,这正是稀疏文件的另一种造法。第三件则轮到了管道:SET、CUR、END、DATA、HOLE 五种 whence 就全军覆没了,一水儿的 ESPIPE(errno 29,非法 seek):管道是流式的,里面没有位置的概念,想定位的,咱们都回去找普通文件。

## 页缓存与 fsync:write 返回不等于数据在盘

`write()` 顺利返回了,也只是保证数据进了**页缓存**(page cache),内核会按它自己的时机回写。man 2 write 的原文说得很硬:**write 成功不提供任何数据已到盘的保证,唯一的确认方式是您调用 fsync(2)**。真正把数据推上盘的三个函数,是各有分工的。`fsync` 干的是数据加元数据全推,一直阻塞到设备报告了传输完成。`fdatasync` 只推影响后续读取的那部分,mtime 之类的可以省,但文件 size 的变化必须推。`sync` 是全盘级的,POSIX 允许它把请求排好了队就返回,而 Linux 的实现则要等 I/O 完成。而把数据真正写到盘上的这件事,是有单价的,我们实测给您看。两段各做 200 次的 `write(4KiB)`,B 段的做法是每次多一个 `fsync`。数据文件必须放在真盘的 ext4 上,而 /tmp 是 tmpfs,放到那儿就白测了:

```text
$ ./exp6
A: 200 x write(4KiB)         :     0.8 ms
B: 200 x (write(4KiB)+fsync) :   229.7 ms
```

A 段的 0.8 ms 摊下来一次是 4μs,这就是纯内存拷贝的价。而 B 段每次多做一个 fsync,单次的开销约 1.15 ms,**慢了近 300 倍**。您现在看到的,就是每一条都保证在盘的单价(环境:WSL2 的 ext4-on-VHDX,编译用的是 g++ -O2,数字是每次会抖的,咱们看量级就好)。

再往深处追的部分,咱们到这儿就停:脏页在 Dirty 计数里滞留多久、进程死了数据到底丢不丢、fsync 与 fdatasync 的元数据差价、O_SYNC 与 O_DIRECT 各自的计时,这些是 [页缓存与持久性](./03-page-cache.md) 一整篇的正题,六个实验专门伺候的就是这件事。本篇您把几条最要紧的立住就够:write 到了页缓存就交差,想要保证的话,咱们就调 fsync,新建的文件,咱们再补一次目录的 fsync。

::: warning 崩溃窗口
从 `write()` 的返回,到内核真正刷下脏页的时候,中间隔着的,是咱们说的崩溃窗口:期间掉电或 panic,数据就没了。而文件系统日志回放之后,您看到的会是一个合法但旧的文件。想要数据在盘的保证,那就请您调 `fsync()`。另外咱们要让文件新建这件事本身变得可靠,还得对**目录**的 fd 再 fsync 一次,否则掉电之后文件可能整个就消失了。
:::

O_APPEND 的好处,咱们放在这里算清楚:多进程写同一份日志的时候,为什么大家都用它?因为内核把挪偏移到文件尾、再完成写入的这两下,合并成了**单个原子步骤**(man 2 open 的原文),并发追加也就不会互相覆盖了。咱们自己 lseek 到尾再 write,那可就是两步了,两个进程都 seek 到了同一个位次,就互相踩了。还请您注意,它保证的只是追加这一步的原子,一次 write 还是可能只写了一部分,所以日志库要把一条日志,攒成一次 write 的量再发出去。

## 收尾:用 unique_fd 与 sys_call 组装一个最小 file

地基打齐了,咱们来组装:`write_all` 是部分写的答案,`read_to_string` 是循环读到 EOF 的答案,fd 咱们一律交给 `unique_fd` 管。下面是 exp7_final.cpp 的下半段,它的上半段没什么新东西,是把 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md) 与[错误处理范式](../../thinking/02-error-paradigm.md) 里定义好的三件公共工具原样搬了过来:

```cpp
void write_all(int fd, std::string_view data)
{
    while (!data.empty()) {
        ssize_t n = sys_call("write", ::write, fd, data.data(), data.size());
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}

std::string read_to_string(const char* path)
{
    unique_fd fd{sys_call("open", ::open, path, O_RDONLY)};
    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = sys_call("read", ::read, fd.get(), buf, sizeof(buf));
        if (n == 0) {
            break;  // EOF 不是错误,sys_call 只拦 -1
        }
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

int main()
{
    const char kPath[] = "/tmp/sysprog-linux01/note.txt";

    {
        unique_fd fd{sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        write_all(fd.get(), "fd born, write, auto-close.\n");
    }  // 离开作用域,~unique_fd() 里 close(fd_)

    std::printf("read back: %s", read_to_string(kPath).c_str());

    try {
        unique_fd bad{sys_call("open", ::open, "/tmp/sysprog-linux01/no_such_file", O_RDONLY)};
    } catch (const std::system_error& e) {
        std::printf("caught: %s (errno=%d)\n", e.what(), e.code().value());
    }
}
```

```text
$ g++ -std=c++23 -O2 -Wall -Wextra exp7_final.cpp -o exp7 && ./exp7
read back: fd born, write, auto-close.
caught: open: No such file or directory (errno=2)
```

错误路径和正常路径的出口,在这里汇成了一个口子:您看 caught 那一行,消息开头的 `open:` 前缀,正是 `sys_call` 的第一个参数,哪一步出的错,屏幕上已经替咱们标好了。咱们走到这里,Linux 侧的地基也就打完了。思维基石两篇给了 `unique_fd`、`sys_call` 与 `errno_code` 定义与设计,本篇带它们打了第一轮实战,后面的姊妹篇里,您会一直看到它们出场。

## 另一侧怎么看

Windows 那边是没有 fd 这个小整数的,`CreateFileW` 返回的是 HANDLE,也就是内核对象表里的一个非透明索引。概念上您可以把 HANDLE 当成 Windows 版的 fd:stdin/stdout/stderr 照样有预留的默认句柄,而 shell 重定向,同样靠子进程继承一个被换过的句柄来实现。

还有几个镜像的差异,咱们挨个混个眼熟。errno 换成了 GetLastError,同样是线程局部的,咱们跨阵营查错误的时候,可别把这两套给查混了。read/write 换成了 ReadFile/WriteFile,同步文件句柄上的 ReadFile,要么把咱们请求的字节数读满,要么停在 EOF 的位置,普通文件上是不会出现短读的。落到咱们的手上,`write_all` 那样的循环,在 Windows 同步文件句柄的普通文件上基本派不上用场,而 POSIX 这边,咱们要是少了这一圈,一次 write 写出去的可能只有一半,剩下的数据就悄悄丢了。

继承的语义正好反过来:Unix 的 fd 默认被子进程继承,咱们不想让它过 exec,就得显式地标上 `O_CLOEXEC`。Windows 的句柄默认一个都不继承,想传下去的句柄,咱们得一个一个显式标记。剩下的细节,咱们到镜像篇里再碰头,您看[Win32 文件 I/O:句柄、CreateFileW 与同步读写](../../windows/file-io/01-win32-file-io.md)。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="open(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/open.2.html"
  />
  <ReferenceItem
    :id="2"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="3"
    title="write(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/write.2.html"
  />
  <ReferenceItem
    :id="4"
    title="dup(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/dup.2.html"
  />
  <ReferenceItem
    :id="5"
    title="pread(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/pread.2.html"
  />
  <ReferenceItem
    :id="6"
    title="fcntl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl.2.html"
  />
  <ReferenceItem
    :id="7"
    title="lseek(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/lseek.2.html"
  />
  <ReferenceItem
    :id="8"
    title="fsync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fsync.2.html"
  />
  <ReferenceItem
    :id="9"
    title="sync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sync.2.html"
  />
  <ReferenceItem
    :id="10"
    title="epoll_ctl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/epoll_ctl.2.html"
  />
  <ReferenceItem
    :id="11"
    title="epoll(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/epoll.7.html"
  />
  <ReferenceItem
    :id="12"
    title="std::system_error"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/system_error"
  />
</ReferenceCard>
