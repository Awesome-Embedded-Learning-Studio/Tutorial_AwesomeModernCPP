---
title: "POSIX 文件 I/O:open/read/write 与 fd 的一生"
description: "Linux 侧文件 I/O 的第一块地基:用 strace 亲眼看完一个 fd 从 open 出生、read/write 干活到 close 落幕的一生——flags 家族与 umask 的交割、部分读写为什么是常态、fd 表与 dup2 重定向的原理、pread 的不动偏移、页缓存与 fsync 的崩溃窗口;顺手就地定义 unique_fd 与 sys_call 这对全系列契约工具,从此告别裸 int fd"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 16
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

咱们看头一处:明明调的是 `open`,trace 里却只见到 `openat` 的影子,欸?您别慌,这当然不是玄学。如今的 `open()` 是 glibc 里的一层兼容外壳,真正发出去的 syscall 是 `openat(AT_FDCWD, ...)`,把这层皮留在用户态的是 glibc,而不是内核。到了 glibc 2.26 这一代,glibc 的 `open` 包装就统一改走了 `openat`,man 2 open 里的 C library/kernel differences 一节,把这件事交代清楚了。有意思的是,x86_64 的内核 syscall 表里 `open` 至今健在,而 arm64/riscv64 这类新架构,才是真的没有了它。

另一处请您看最后一行:`read(3, ..., 64) = 13`,咱们明明申请了 64 字节,结果只拿到 13。其实整个文件也就 13 字节,读到了文件尾,它自然就收了工、而不是报了错。部分读写是 POSIX 的常态,咱们后面专门展开。

边界也顺手交代一下:C 的 `fopen`/`fread` 卷一讲过([C 文件 I/O 与 stdlib](../../../../vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md)),`std::filesystem` 咱们留给本卷后续讲目录与元数据的篇章,mmap 是下一篇([把文件贴进地址空间](./02-mmap-memory-mapping.md))的主角,而文件锁和 inotify,咱们放到本卷后续章节再说。错误处理的这边,本文用的类型底座还是 `std::error_code` 和 `std::system_error`,体系的来龙去脉,vol3 的 [error_code 深入](../../../../vol3-standard-library/error-utils/66-error-code.md) 已经完整讲过,咱们不在这里再讲一遍。而 Linux 侧怎么把它们用得顺手,咱们本篇就地定义一对小工具,您到后面错误处理的小节里就能见到它们。

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

round 6 的请求是 16,可读的只剩 4。round 7 返回的 0 就是 EOF,流到头了,而不是错误。`write` 也是同样的道理:返回值是实际写入的数,有可能比请求的少。落到咱们手里的,其实只有一条守则:read/write 的调用必须循环,一次调用成功了,不代表事情做完了,后面的 `write_all`,就是把这个循环封装起来的产物。另外提醒您一句,您别急着把这些 fd 塞给 epoll:普通文件进不了 epoll,`epoll_ctl` 会直接给您 `EPERM`,原话就收在参考资源里 man 2 epoll_ctl 的条目里。普通文件永远是就绪的,多路复用本来就是 socket 与管道的世界。epoll 机制本身是怎么运转的,您可以到 [网络卷的 epoll 一篇](../../../networking/02-epoll-io-multiplexing.md) 去看。

## 错误处理:本系列的 Linux 侧工具箱

syscall 的调用一旦失败,返回的就是 -1,细节全留在了 errno 里。既然每次调用都有失败的可能、失败了就要看 errno,咱们这个系列干脆把错误处理收进了两个小工具:在本篇定义、全系列引用。下面的两段代码截自同一个编译单元,用 `g++ -std=c++23 -O2 -Wall -Wextra` 的配置编译,拿到的是零警告,include 咱们就略去了:

```cpp
// 契约一:非抛路径。errno 是线程局部的,读进 error_code 就定格了
std::error_code errno_code() noexcept
{
    return std::error_code{errno, std::generic_category()};
}

// 契约二:任何「返回 -1 表失败」的 syscall 都从这儿过
template <class F, class... Args>
auto sys_call(const char* what, F&& f, Args&&... args)
{
    auto result = std::forward<F>(f)(std::forward<Args>(args)...);
    if (result == -1) {
        throw std::system_error{errno, std::generic_category(), what};
    }
    return result;
}
```

咱们用起来就是 `sys_call("open", ::open, path, flags, mode)`:失败抛 `std::system_error`,errno 装进了 `generic_category`(POSIX errno 恰好就是 `std::errc` 的值域),`what` 则成为异常消息的前缀。咱们 catch 到手,一眼就知道是哪一步炸的。还要请您留意一处:read 返回 0(EOF)并不是 -1,`sys_call` 的处理是原样放行、把 EOF 留给调用方去判断,这是刻意的设计。

## unique_fd:把 close 写进析构函数

裸 `int fd` 的毛病,写过程序的您多半领教过:函数里写了三条提前 return,`close` 写在了第四条路径上,而不泄漏的保障,全靠人肉的 review。咱们这个系列的解法,是一个 move-only 的 RAII 包装,同样地,咱们全系列也只定义它一次:

```cpp
class unique_fd
{
public:
    explicit unique_fd(int fd = -1) noexcept : fd_(fd) {}
    unique_fd(unique_fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    unique_fd& operator=(unique_fd&& other) noexcept
    {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    ~unique_fd() { reset(); }

    int get() const noexcept { return fd_; }
    int release() noexcept { return std::exchange(fd_, -1); }
    explicit operator bool() const noexcept { return fd_ >= 0; }
    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_;
};
```

移动构造掏空的是对方。而移动赋值把自己 `reset()` 掉,再接管新来的 fd。析构走的是 `reset()`,落到了代码上,看到的就是 `if (fd_ >= 0) ::close(fd_)`。心智模型和 `unique_ptr` 是同构的,只是标的从堆指针换成了 fd。`operator bool` 特意标了 `explicit`,防的就是 `if (fd)` 这样把 fd 当布尔的糊涂用法。它到底能防住什么?光说不过瘾,我们故意漏一次试试:程序开头遍历 `/proc/self/fd` 打印现有的 fd,再用 `setrlimit` 把 RLIMIT_NOFILE 压到了 8,然后咱们循环 `open` 同一个文件,偏偏故意不 close:

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

接着 4、6、7 一路漏了过去(5 号被占了,跳过了),第 5 个 open 顶上了 RLIMIT_NOFILE=8。errno 24 对应的就是 `EMFILE`。真实环境的额度,您看 `ulimit -n` 就知道,笔者的 WSL 是 10240,老发行版里常见的是 1024。长跑的服务每接一个请求就漏一个 fd,额度就是这么一点点吃光的。而泄漏掉的 fd,要等到进程退出了才释放,中途是没有谁会还的。

## dup/dup2:shell 的重定向是这么来的

咱们想把 fd 表看明白,就得知道它的两级结构:一级是进程私有的 fd 表,每一项都指向系统级的**打开文件描述**(open file description),偏移量和状态标志都存在后者的身上。`open` 了两次,得到的是两个独立的 description,各写各的偏移。`dup` 和 `fork` 复制的是 fd 表项,于是两个 fd 就指向了同一个 description,共享了偏移。fork 了之后,父子进程接着对方的偏移往下写,而奥妙就在这里,细节咱们留到进程篇再展开。

`dup2(fd, 1)` 干的事更直接,咱们看代码:它把 fd 表的 1 号槽位,原子地换成指向 fd 的文件,此后一切 `write(1, ...)`、`printf` 就全进了文件。shell 的 `> file`,正是 fork 完了、exec 还没跑的那一下(exp4_dup2.cpp 的节选,咱们省去 include 与 main 的外壳):

```cpp
int fd = open("/tmp/sysprog-linux01/out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
int saved_stdout = dup(1);     // 先留一条回终端的路
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

再补一个容易漏的冷知识:dup 出来的新 fd **不带** `FD_CLOEXEC`,哪怕原来的 fd 带了。man 2 dup 的原文写得很明确,close-on-exec 是不共享的。您要带着它过 exec 边界,这个标志位就得重新检查一遍了。

## lseek/pread/pwrite:偏移是内核那头的状态

偏移量不在您的代码里,它住在内核的 open file description 里:read/write 的调用推进它,`lseek` 的调用改它。多线程共享一个 fd 的并发读,就等于共享一支会被彼此挪动的隐形游标。`pread`/`pwrite` 则把偏移变成了调用参数,一次调用等价于原子的 lseek+read,而且读完了也不动游标。咱们用内容为 "0123456789" 的文件实测:第一步咱们 `pread(fd, buf, 4, 2)`,从偏移 2 的位置读 4 个字节。第二步用 `lseek(fd, 0, SEEK_CUR)` 查一下 fd 当前的偏移,随后再用普通的 `read(fd, buf, 4)` 做对照:

```text
$ ./exp8
pread(4, off=2) -> "2345", fd offset now 0
read(4)         -> "0123", fd offset now 4
```

`pread` 从偏移 2 那里拿到了 "2345",fd 的偏移纹丝不动,停在了 0 上。紧接着的普通 `read`,才是从 0 起步的。正是这一点让 pread 成了多线程并发读同一文件时,咱们最省心的姿势,省去了加锁的麻烦,也不会踩到别人的游标。不过 Linux 在这里有个小脾气:在用 `O_APPEND` 打开的 fd 上,`pwrite` 会无视传入的偏移直接追加。man 2 pread 也明说了,这并不符合 POSIX 的要求,而是 Linux 自己的历史行为。

## 页缓存与 fsync:write 返回不等于数据在盘

`write()` 顺利返回了,也只是保证数据进了**页缓存**(page cache),内核会按它自己的时机回写。man 2 write 的原文说得很硬:**write 成功不提供任何数据已到盘的保证,唯一的确认方式是您调用 fsync(2)**。真正把数据推上盘的三个函数,是各有分工的。`fsync` 干的是数据加元数据全推,一直阻塞到设备报告了传输完成。`fdatasync` 只推影响后续读取的那部分,mtime 之类的可以省,但文件 size 的变化必须推。`sync` 是全盘级的,POSIX 允许它把请求排好了队就返回,而 Linux 的实现则要等 I/O 完成。而把数据真正写到盘上的这件事,是有单价的,我们实测给您看。两段各做 200 次的 `write(4KiB)`,B 段的做法是每次多一个 `fsync`。数据文件必须放在真盘的 ext4 上,而 /tmp 是 tmpfs,放到那儿就白测了:

```text
$ ./exp6
A: 200 x write(4KiB)         :     0.8 ms
B: 200 x (write(4KiB)+fsync) :   229.7 ms
```

A 段的 0.8 ms 摊下来一次是 4μs,这就是纯内存拷贝的价。而 B 段每次多做一个 fsync,单次的开销约 1.15 ms,**慢了近 300 倍**。您现在看到的,就是每一条都保证在盘的单价(环境:WSL2 的 ext4-on-VHDX,编译用的是 g++ -O2,数字是每次会抖的,咱们看量级就好)。

::: warning 崩溃窗口
从 `write()` 的返回,到内核真正刷下脏页的时候,中间隔着的,是咱们说的崩溃窗口:期间掉电或 panic,数据就没了。而文件系统日志回放之后,您看到的会是一个合法但旧的文件。想要数据在盘的保证,那就请您调 `fsync()`。另外咱们要让文件新建这件事本身变得可靠,还得对**目录**的 fd 再 fsync 一次,否则掉电之后文件可能整个就消失了。
:::

O_APPEND 的好处,咱们放在这里算清楚:多进程写同一份日志的时候,为什么大家都用它?因为内核把挪偏移到文件尾、再完成写入的这两下,合并成了**单个原子步骤**(man 2 open 的原文),并发追加也就不会互相覆盖了。咱们自己 lseek 到尾再 write,那可就是两步了,两个进程都 seek 到了同一个位次,就互相踩了。还请您注意,它保证的只是追加这一步的原子,一次 write 还是可能只写了一部分,所以日志库要把一条日志,攒成一次 write 的量再发出去。

## 收尾:用 unique_fd 与 sys_call 组装一个最小 file

地基打齐了,咱们来组装:`write_all` 是部分写的答案,`read_to_string` 是循环读到 EOF 的答案,fd 咱们一律交给 `unique_fd` 管。下面是 exp7_final.cpp 的下半段,它的上半段,就是前面已经定义好的 errno_code、sys_call 与 unique_fd:

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

错误路径和正常路径的出口,在这里汇成了一个口子:您看 caught 那一行,消息开头的 `open:` 前缀,正是 `sys_call` 的第一个参数,哪一步出的错,屏幕上已经替咱们标好了。咱们走到这里,Linux 侧的地基也就打完了。而 `unique_fd`、`sys_call` 与 `errno_code`,自此就成了全系列的公共词汇,后面各篇咱们只管引用,不再重定义了。

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
    title="fsync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fsync.2.html"
  />
  <ReferenceItem
    :id="7"
    title="sync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sync.2.html"
  />
  <ReferenceItem
    :id="8"
    title="epoll_ctl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/epoll_ctl.2.html"
  />
  <ReferenceItem
    :id="9"
    title="epoll(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/epoll.7.html"
  />
  <ReferenceItem
    :id="10"
    title="std::system_error"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/system_error"
  />
</ReferenceCard>
