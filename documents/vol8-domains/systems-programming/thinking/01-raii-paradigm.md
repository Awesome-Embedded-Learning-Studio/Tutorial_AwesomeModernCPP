---
title: "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
description: "系统编程概念篇第一篇:fd、HANDLE、内存映射全是拿到手就必须还的 OS 资源,本篇把 close/CloseHandle/munmap 写进析构函数,给出全系列唯一定义处的 unique_fd、unique_handle、mapped_region 三副 move-only 骨架;实测异常路径裸 fd 漏 1000 个而 RAII 版一个不漏、析构里 close 返回值按 man 2 close 的口径丢弃(不重试也不抛,在乎 I/O 错误就在 close 前 fsync)、Windows 失败值 -1 与 NULL 两套并存、CloseHandle 对伪句柄静默返回 TRUE、GetProcessHandleCount 全程作证、release/reset/swap 归还语义、vector 扩容搬迁 7 次而 shuffle 零移动,以及移动不标 noexcept 会退拷贝这一说法对 move-only 类型不成立的实测纠正"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 25
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
related:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "mmap 内存映射:把文件贴进地址空间"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - Win32
  - RAII
  - 移动语义
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架

咱们在[总纲](../00-overview.md)里把两大阵营认了门,这一篇咱们就该动手干正事了。正事的第一件,笔者不想从任何一个具体 API 讲起,而是想跟您聊一类共同的麻烦:fd(file descriptor,文件描述符)、HANDLE(Windows 的句柄)、内存映射,再加上后面的进程和信号量,它们全是**拿到手就必须还**的东西。open 出来的 fd 要 close,CreateFileW 拿到的句柄要 CloseHandle,mmap 贴进来的那一段地址要 munmap。哪一次忘了还,资源就挂在进程身上不走了,而要等到进程退出,才有内核替咱们收拾。

麻烦的根子在 C 的调用约定上:归还这一步是一句独立的调用,它和获取动作之间没有任何语法上的绑定。您在函数里 open 了一个 fd,这个函数可能有正常的返回、三个错误分支,还可能有被异常掀翻的时候,每一条离开的路,咱们都得记得补一句 close。写的时候人人都知道,三个月后往里加第四个分支的多半还是您自己,一旦忘掉了,它就漏了。空口说漏没什么意思,咱们直接把伤口摆出来。

咱们在动手之前,把两边实验室的口径一次交代清楚,后面的数字都要靠它对表。Linux 侧的实验全部出自笔者的 WSL2,内核跑的是 6.18.33.2-microsoft-standard-WSL2,g++ 用的是 16.2.1,strace 用的是 7.2,CPU 是 AMD Ryzen 的 9700X,编译全部走的是 `g++ -std=c++20 -Wall -Wextra -O2`,拿到的警告数是零。Windows 侧出自笔者的 Win11(版本号 26200.9457,系统语言是中文的),编译器是 MSYS2 UCRT64 的 g++ 16.1.0。编译和运行咱们都在 WSL 里做,靠的是 WSL 的 interop 通道,它让您在 Linux 侧直接调起 Windows 的程序。实操上的要点有三个:咱们 cd 到源码目录,把调 `g++.exe` 的参数全换成相对路径。产物落在 WSL 的文件系统上是不带执行位的,得 `chmod +x` 补一次才能跑了。全部代码与原始输出收在仓库 `code/volumn_codes/vol8/systems-programming/` 下面 linux 与 windows 两侧的 `thinking/01-raii-paradigm/`,捕获的日期是 2026-10-02,您想核对的话,它们随时等着您去翻。

## 裸 fd 是怎么漏的:一千次异常,一千个没关的 fd

咱们写一个最小化的泄漏现场:循环 N 次,咱们每次都在 try 块里 open 一个文件,然后就 throw 了。异常离开作用域的时候,裸 fd 是没有谁会替它收尾的,catch 里咱们也故意不补救:

```cpp
// leak_raw.cpp 的核心循环:裸 fd,异常路径每次漏一个
for (int i = 0; i < n; ++i) {
    try {
        int fd = ::open(kPath, O_RDONLY);   // 裸 fd,没有包装
        if (fd == -1) { ++open_failures; continue; }
        (void)fd;
        throw std::runtime_error("error path leaves the scope now");
    } catch (const std::exception&) {
        ++caught;   // catch 里也没有 close:fd 就这么留下了
    }
}
```

程序前后各读一遍 `/proc/self/fd`(这个目录把进程当前打开的 fd 全摊开成文件,总纲里咱们用过它),前后一相减就得到了泄漏数。咱们实测跑出来的数字:

```text
$ ./leak_raw 1000
RLIMIT_NOFILE: soft=1048576 hard=1048576
fds before (3): 0 1 2
fds after  (1003): 0 1 2 3 4 5 6 7 ... 999 1000 1001 1002
loop=1000 caught=1000 open_failures=0 leaked=1000
```

循环跑了一千次,漏掉的正好一千个:进程起点的 0/1/2 三个标准流之外,fd 表一路涨到了 1003。您可能会问,这程序也太假了,谁会 open 完马上 throw?形式确实极端,漏法却是通用的:把 throw 换成任何一条提前的 return、一个 continue、一次循环里的 break,漏掉的都是同样一批,而异常只是漏得最无声的一种,因为离开作用域的那个点,压根不写在您的代码里。

咱们顺路记两个环境事实。头一个说的是额度:`RLIMIT_NOFILE` 管的是 fd 数量的上限,`ulimit -n` 看的就是它,笔者的 WSL 现在是 soft=hard=1048576,所以 1000 次全都开成功了。额度为 1024 的老环境里,把 N 加大了再跑,open 就会在 fd 表塞满的那一刻遇上 EMFILE(errno 24,额度耗尽了)。那个现场咱们不陌生,[POSIX 文件 I/O](../linux/file-io/01-posix-file-io.md) 的 exp5 演过,setrlimit 把额度压到了 8,专门造了一次 EMFILE。另一个说的是起点:只有 0/1/2 这件事并不是常数,WSL 塞给进程的继承 fd 会随启动链漂移,同一段的 exp5 起跑时就多出过 5 和 10 两个,咱们别拿它当基准背下来。

对照组咱们就改一个词:open 出来的 fd 交给 `unique_fd` 包装,别的都不动。跑出来的输出长这样:

```text
$ ./leak_raii 1000
RLIMIT_NOFILE: soft=1048576 hard=1048576
fds before (3): 0 1 2
fds after  (3): 0 1 2
loop=1000 caught=1000 leaked=0
```

同样的一千次异常,`leaked=0`。两份输出一前一后地摆着,也就是本篇全部的动机了:漏不漏,不该取决于您记性好不好,而该取决于类型。接下来咱们把这副类型亲手造出来。

## unique_fd:把 close 写进析构函数

C++ 给这套解法留了正名:RAII(Resource Acquisition Is Initialization、资源获取即初始化),Stroustrup 起的名字,[vol2 的 RAII 深入](../../../vol2-modern-features/ch01-smart-pointers/01-raii-deep-dive.md)已经陪您走过一遍语言层。落到咱们眼下的这件事,思路其实只有一句:把资源连同生命周期包进一个栈上的对象,构造的时候获取,析构的时候归还。语言替咱们保证了,不管离开作用域的路子是正常走完、提前 return,还是异常引发的栈展开,栈上对象的析构函数都会执行。从此 close 就不用人记得去写了,它变成了一句“对象没了,资源就没了”。

下面就是 `unique_fd` 的完整体,它是咱们这个系列的公共词汇,全系列只在咱们这儿定义一次,后面 Linux 侧的篇章直接引用。代码出自实验存档里的 `common/raii.hpp`,编译的口径是 `g++ -std=c++20 -Wall -Wextra -O2`,拿到的警告数是零:

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

    // 放弃所有权:fd 交还调用方,此后析构不再 close
    int release() noexcept { return std::exchange(fd_, -1); }

    explicit operator bool() const noexcept { return fd_ >= 0; }

    // 把手里现有的 close 掉,再接管新 fd;无参调用 = close + 变空
    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            ::close(fd_);   // 返回值直接丢弃,理由见下一节
        }
        fd_ = fd;
    }

    void swap(unique_fd& other) noexcept { std::swap(fd_, other.fd_); }
    friend void swap(unique_fd& a, unique_fd& b) noexcept { a.swap(b); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

private:
    int fd_;
};
```

咱们把它一行行过一遍。拷贝构造与拷贝赋值整个被 `= delete` 删掉了:fd 的所有权只能有一条,要是咱们真弄出两个 unique_fd 都认为自己拿着 fd 3,析构的时候就会把同一个 fd 关两次。把拷贝删掉了,这个类型就只剩了移动一条路,也就是咱们在 vol2 的[移动语义](../../../vol2-modern-features/ch00-move-semantics/02-move-semantics.md)里见过的 move-only 类型:移动构造把对方的 fd 拿过来,再把对方掏空成了 -1。移动赋值把自己 reset 掉了,再去接管新来的。析构统一走的是 reset。心智模型和 `std::unique_ptr` 是完全同构的,只是标的从堆指针换成了一个 int。

`explicit operator bool` 那个 explicit 是有意的,不过它挡的东西,和很多人以为的不一样。`if (fd)` 这样的条件语境它根本不挡:语境转换本来就给 explicit 留了门,您写 `if (fd)` 编得好好的。它真正拦下的,是把对象当值用的隐式转换。咱们当场编过一个只有这行成员的最小类来验,`bool ok = obj;` 这一句 GCC 回的是 `cannot convert to 'bool' in initialization`。咱们再看 `obj == 0`,它报的是 no match for 'operator=='。要是咱们去掉这个 explicit,这两句就都能悄悄编过了,对象就这么被当成布尔值拿去做算术、去和整数比大小了,隐患也就埋得很深了。`release`、`reset`、`swap` 三个都是所有权的出口与换手,咱们留到后面专门一节演。

配套的还有两个十行以内的小工具,后面咱们写示例代码要用到它们:

```cpp
// errno 装箱:errno 是线程局部的,读进 error_code 就定格了
inline std::error_code errno_code() noexcept
{
    return std::error_code{errno, std::generic_category()};
}

// 任何返回 -1 表失败的 syscall 都从这儿过:失败抛 system_error,what 带前缀
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

咱们就这么用:`sys_call("open", ::open, path, flags, mode)`,失败的时候抛 `std::system_error`,异常消息里带着 `open:` 的前缀,哪一步炸的一眼可见。errno 与 GetLastError(Windows 侧报告错误的通道,和 errno 一样按线程各存一份)怎么装箱、错误在工具层和应用层怎么分流,这套完整的故事归本系列的下一篇《错误处理范式:从 errno 到 expected》,本篇咱们只借用它们的最小形态。

## strace 之下:close 一对一对地发生

析构函数真的会去 close,这话咱们不该靠信。咱们写个四幕的小程序:建文件,正常地离开一个作用域,在函数里 open 完了直接 return(一个 close 都不写),再建了一段映射。最要紧的是第三幕,它的代码是这样的:

```cpp
// 提前 return 的典型现场:错误分支各写一个 close 的活,交给析构
static bool early_return_demo()
{
    unique_fd fd{
        sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
    write_all(fd.get(), "written just before the early return\n");
    std::printf("[early-return] fd=%d still open, returning now\n", fd.get());
    return false;   // 一个 close 都没写,~unique_fd() 在栈上收尾
}
```

然后咱们把 strace 请出来,咱们只让它跟踪 openat 和 close 这两类。它会把进程发出的系统调用逐条记下来,咱们数一数配对:

```text
$ strace -f -e trace=openat,close ./strace_demo
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
close(3)                                = 0
...（动态链接器开 .so 的另外 4 对 openat/close,全带 O_CLOEXEC,略）...
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
close(3)                                = 0
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_RDONLY) = 3
close(3)                                = 0
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
close(3)                                = 0
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_RDONLY) = 3
close(3)                                = 0
+++ exited with 0 +++
```

trace 尾巴上的四对,正好对应程序的四幕:建文件、读、提前 return 的那一次写,以及映射前的那一次读。请您把镜头对准第三对——它就是 early_return_demo 的 fd,咱们写它的时候一个 close 都没写,close 还是来了,就落在那次 return 的路上。这就是析构在干的活,而且是每一条离开路径上都干。至于开头的加载器那几对:动态链接器打开 .so 全带 `O_CLOEXEC`,这个现象 [POSIX 文件 I/O](../linux/file-io/01-posix-file-io.md) 那篇的旁注里专门记着,不是实验本身的。

## 析构里的 close 返回值:EINTR 的正确处置

骨架立起来了之后,有一个问题咱们躲不掉了:`close()` 自己会失败吗?会,而且 man 2 close 的 ERRORS 里列的不止两位。EBADF 说的就是 fd 不合法,这等于宣告程序里已经有别的 bug 了,没有补救的意义。EIO 则是真正的 I/O 错误。ENOSPC 与 EDQUOT(空间耗尽、配额耗尽)在 NFS 这类文件系统上还有句特别注明:超支的那一笔往往头一次写时不报,要拖到后面的 write、fsync 或者 close 才浮出来。真正麻烦的则是 EINTR(errno 值,意思是被信号打断了)。

很多人在这里的直觉是“被打断了,那就再关一次”。man 页 NOTES 一节的标题就叫 Dealing with error returns from close(),咱们把它的原话请出来:

> 笔者把原话一字未动地抄在这里:`Retrying the close() after a failure return is the wrong thing to do, since this may cause a reused file descriptor from another thread to be closed.`

为什么重试是错的?因为 Linux 内核总是在 close 操作的早期就释放了 fd 本身,此后这个编号随时可能被别的线程的 open 拿去复用,真正可能出错的步骤(向文件系统或设备冲刷数据)发生在后半段。您收到一个失败的 close,咱们再关一次,第二次落下去的可能是别人刚拿到手的新 fd,好心办了坏事。

EINTR 还有一段特殊的身世。POSIX.1-2008 只说此时 fd 的状态未指定。Linux 与多数实现的行为是,返回 EINTR 的时候 fd 保证已经关掉了,内核只是顺带报告了一声有信号来。HP-UX 则是文档写明的反例,它的 fd 在 EINTR 时还开着,咱们必须再关一次。到了 POSIX.1-2024,标准把 HP-UX 的行为定了下来,Linux 也因此成了“不合新标准”的一员,man 页也明说了没有修改的计划。咱们不用替内核尴尬,记下 Linux 的实际行为就行。

落到咱们的析构函数里,采纳的口径只有一条:**close 的返回值直接丢弃**。咱们不重试,理由在上面说过了。咱们也不抛,析构函数天生就跑在 noexcept 的语境里,这里抛出去的下场就是 std::terminate,代价比漏一个 fd 惨多了。被丢弃的这个返回值,本来能告诉咱们什么?就是 close 后半段那次冲刷里,有没有真的遇上 I/O 错误。真在乎这件事的话,man 页替咱们这些细心程序员指了正路:真想知道 I/O 错误的话,咱们就在 close 之前对同一个 fd 调 fsync(2),并检查 fsync 自己的返回值。它同时也在告诫咱们,不查 close 的返回值可能导致数据无声丢失,NFS 和磁盘配额就是典型的场合。页缓存与 fsync 的完整故事,您到 [POSIX 文件 I/O](../linux/file-io/01-posix-file-io.md) 的对应小节会读到。眼下咱们只认一条:该问的问题,咱们挪到 fsync 那里去问。

## mapped_region:换一个标的,同一副骨架

接下来的这一步,咱们把同一副骨架套上第二类资源:内存映射。mmap 把文件贴进地址空间,归还的事则由 munmap 负责,漏掉它漏的是一段地址空间。包装的骨架和 unique_fd 一模一样,move-only 的做法、析构走 reset 的收尾,只有两处需要您多看两眼,咱们放在代码后面:

```cpp
class mapped_region
{
public:
    // release() 的带走清单:想手动 munmap,要的正是这两样
    struct released {
        unsigned char* addr;
        std::size_t length;
    };

    mapped_region() noexcept = default;

    mapped_region(const unique_fd& fd, std::size_t length, int prot, int flags,
                  off_t offset = 0)
    {
        // mmap 失败返回 MAP_FAILED(即 (void*)-1)并设 errno,不是 nullptr
        void* p = ::mmap(nullptr, length, prot, flags, fd.get(), offset);
        if (p == MAP_FAILED) {
            throw std::system_error{errno_code(), "mmap"};
        }
        addr_ = static_cast<unsigned char*>(p);
        const std::size_t page =
            static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
        length_ = (length + page - 1) / page * page;   // 按页向上取整,记下真实长度
    }

    mapped_region(mapped_region&& other) noexcept
        : addr_(other.addr_), length_(other.length_)
    {
        other.addr_ = nullptr;
        other.length_ = 0;
    }

    mapped_region& operator=(mapped_region&& other) noexcept
    {
        if (this != &other) {
            reset();
            addr_ = std::exchange(other.addr_, nullptr);
            length_ = std::exchange(other.length_, 0);
        }
        return *this;
    }

    ~mapped_region() { reset(); }

    void reset() noexcept
    {
        if (addr_ != nullptr) {
            ::munmap(addr_, length_);   // 失败多半意味着地址或长度已经错了,吞掉返回值
            addr_ = nullptr;
            length_ = 0;
        }
    }

    released release() noexcept
    {
        return released{std::exchange(addr_, nullptr),
                        std::exchange(length_, 0)};
    }

    unsigned char* data() const noexcept { return addr_; }
    std::size_t size() const noexcept { return length_; }   // 取整后的映射长度
    explicit operator bool() const noexcept { return addr_ != nullptr; }

    // reset(addr, length):接管一段裸映射的重载,略
    // swap:成员与 friend 各一份,形态与 unique_fd 那对相同,略

    mapped_region(const mapped_region&) = delete;
    mapped_region& operator=(const mapped_region&) = delete;

private:
    unsigned char* addr_ = nullptr;
    std::size_t length_ = 0;
};
```

头一处的麻烦在失败值。mmap 失败返回的是 `MAP_FAILED`(也就是 `(void*)-1`)并设置 errno,给的不是 nullptr,所以 `sys_call` 那套“拿返回值和 -1 比”的检查在这里编不过,void* 是没法和整数比的,构造函数里咱们手写判断。用 `if (p)` 查失败同样是查不出来的,它对 `(void*)-1` 是恒为真的。

第二处的讲究是长度要补到页边界。内核替咱们把 length 按页向上取整,munmap 要用的正是取整之后的长度,于是构造函数里咱们自己算好了记下,`size()` 报的也是取整后的长度。这事在 syscall 的层面看得见,咱们把 strace 换成跟 mmap、munmap:

```text
$ strace -f -e trace=mmap,munmap ./strace_demo
...（加载器把 libc 等映射进来的二十来行 mmap,略）...
mmap(NULL, 16, PROT_READ, MAP_PRIVATE, 3, 0) = 0x78f3972c3000
munmap(0x78f3972c3000, 4096)     = 0
+++ exited with 0 +++
```

咱们申请的是 16 字节,内核贴进来的是一整页,析构归还的时候按 4096 交还。程序自己的打印也印证了它:`asked 16 B, region claims 4096 B`。`release()` 交还调用方的是 `released` 结构体,地址和长度两样一起带走,正好是手动 munmap 需要的全部参数。至于 mmap 六个参数各自的行情、越界会吃到什么信号、写时复制怎么工作,那是 [mmap 内存映射](../linux/file-io/02-mmap-memory-mapping.md) 那一整篇的正题,咱们这里不抢。

## Windows 侧:unique_handle 与两套失败值

轮到 Windows 的时候,咱们把同一副骨架换上那边的零件:fd 换成 HANDLE(不透明的指针型值),close 的差事交给 CloseHandle,空值 -1 换成了 `INVALID_HANDLE_VALUE`(它也是 -1)。别的部分一个字不用改,这就是两大阵营在 C++ 这一层汇合的地方。咱们在包含 `<windows.h>` 之前,得提前把两个宏定义好:`WIN32_LEAN_AND_MEAN` 替咱们挡掉一批这次用不着的头,`NOMINMAX` 则把 windows.h 里的 min/max 宏拦下来,不拦的话,它们就要跟 `std::min`/`std::max` 撞名了:

```cpp
class unique_handle
{
public:
    explicit unique_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_handle(unique_handle&& o) noexcept
        : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_handle& operator=(unique_handle&& o) noexcept
    {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~unique_handle() { reset(); }

    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept
    {
        if (h_ != INVALID_HANDLE_VALUE) { ::CloseHandle(h_); }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};
```

骨架是没有问题的,真正的分岔在失败值上。POSIX 侧好歹统一了,syscall 失败返回的一律是 -1。Win32 这边却是同时立着两个失败哨兵的。哨兵值(sentinel value)这个说法咱们现在用上了:它指 API 约定拿来标记失败的特定返回值,POSIX 的 -1 就是其中一枚,Windows 这边的两枚,长相可就差远了。咱们把实测的矩阵摆开(错误码是紧跟其后的 GetLastError):

| API(失败的姿势)             | 失败时的返回值                     | GetLastError |
| ----------------------------- | ---------------------------------- | ------------ |
| CreateFileW(OPEN_EXISTING,文件不存在) | INVALID_HANDLE_VALUE(十六进制 ffffffffffffffff) | 2(ERROR_FILE_NOT_FOUND) |
| CreateEventW(与已有互斥体撞名)       | NULL                               | 6(ERROR_INVALID_HANDLE) |
| CreateFileMappingW(与已有互斥体撞名) | NULL                               | 6(ERROR_INVALID_HANDLE) |
| OpenProcess(野 pid)                  | NULL                               | 87(ERROR_INVALID_PARAMETER) |

CreateFileW 一家失败了给 -1,而 CreateEventW 失败给的是 NULL,CreateFileMappingW 给的也是 NULL,OpenProcess 也是同样的。判错判反了,等咱们的就是静默 bug,所以动手写判错以前,请您翻一遍文档的 Return value 段,光靠背是不行的。矩阵之外咱们还留了一行对照,笔者觉得最有意思:`CreateFileMappingW(INVALID_HANDLE_VALUE, ...)` **不是失败**,这个 -1 传进去的意思是“页文件支持的映射”:页文件(page file)是 Windows 的虚拟内存后备文件,拿它建的映射不挂任何的文件,数据落的是页面文件。实测里它成功发回了有效句柄。您品一下 -1 的双重身份:在 CreateFileW 的返回值位上,它代表的就是失败。换到 CreateFileMappingW 的参数位上,它就成了合法入参。同一个数换到别的位置,身份就全变了。

> 返回 BOOL 的那批 API(ReadFile/WriteFile/CloseHandle 这些)失败就是 FALSE,细节咱们同样查 GetLastError。判这两套哨兵的活儿,咱们有现成的模板可用:check_win32,它用 if constexpr 按返回类型做了分流,完整的交代在 [Win32 文件 I/O](../windows/file-io/01-win32-file-io.md) 那篇里。

## CloseHandle 的哨兵反应与一个 NULL 陷阱

unique_handle 拿 `INVALID_HANDLE_VALUE` 当它的空哨兵,那 CloseHandle 自己对各种奇怪输入是什么反应?咱们做一个很小的实验:用 `SetLastError(1234)` 往 last-error 槽里放一个哨兵值,然后咱们对五种输入各调一次 CloseHandle,返回值和哨兵的动静,咱们都看到了:

```text
$ ./e1b_closehandle_sentinel
CloseHandle(NULL                   =                0) -> ret=0 err=6
CloseHandle(INVALID_HANDLE_VALUE   = ffffffffffffffff) -> ret=1 err=1234
CloseHandle(GetCurrentProcess()    = ffffffffffffffff) -> ret=1 err=1234
CloseHandle((HANDLE)-2             = fffffffffffffffe) -> ret=1 err=1234
CloseHandle((HANDLE)0x1234         =             1234) -> ret=0 err=6
```

咱们一行行看。NULL 和野值 0x1234 拿到的都是 FALSE 加 err=6(ERROR_INVALID_HANDLE),哨兵被它覆盖了,属于正常的失败。中间三行就有意思了:返回 TRUE,而且 err 还是 1234,咱们放进去的哨兵原封不动,全程没人碰过 last-error 的槽。-1 和 -2 是**伪句柄**(pseudo handle):GetCurrentProcess() 返回的是 -1,GetCurrentThread() 返回的是 -2,它们是不占句柄表的,每次被用到的时候才现场解析成“当前进程/当前线程”。咱们拿伪句柄去调 CloseHandle,它压根没有可关的槽位,于是咱们什么也没看到被关掉,它却给咱们返回了 TRUE。

这个 TRUE 能不能当判断依据?不能。关于拿伪句柄去关的这件事,文档写明的只有一句,GetCurrentProcess 的 Remarks 写的是,拿伪句柄调 CloseHandle 是没有效果的(no effect),关了等于没关。至于返回 TRUE、last-error 槽纹丝不动的细节,那就是笔者在 Win11 26200 上实测的口径了,文档是没写到这一层的。文档的 Return value 段倒是另有一条:程序挂在调试器下跑的时候,CloseHandle 收到的是无效句柄**或伪句柄**,它就直接抛异常了。平时它静默地回 TRUE,换到调试器下就可能当场炸了,这个组合本身就在告诉咱们,别写任何依赖这个 TRUE 的代码。双关闭的下场咱们也记下了:同一个有效句柄关两次,第二次拿到的也是 FALSE 加 err=6,待遇跟 NULL 是一样的。

那 unique_handle 的生命周期,拿什么作证?Windows 给了现成的读数函数:GetProcessHandleCount,把进程当前持有的句柄总数读出来,任务管理器里的“句柄”列读的就是它。咱们给 move 全套的每一步都读一次计数。下面这块输出连同稍后 NULL 陷阱的那一块,都出自同一个实验程序的节选,它整个分成了五段,咱们只搬了 [3] 生命周期与 [4] 陷阱这两段,编号沿用的就是存档:

```text
[3] unique_handle 生命周期(数字 = 进程当前句柄数)
  baseline                       : 61
  构造 a(接管 CreateFileW)      : 62  bool(a)=1 get=00000000000000f8
  move 构造 b<-a                 : 62  bool(a)=0 bool(b)=1
  b.release() 所有权交还         : 62  bool(b)=0 raw=00000000000000f8
  手动 CloseHandle(raw)          : 61
  a.reset(新句柄)                : 62  bool(a)=1
  a.reset() 立即关闭             : 61  bool(a)=0
  move 赋值 c<-临时              : 62
  作用域结束(析构全部执行)     : 61  (回到 baseline=61)
```

咱们把这串数字对着操作读:构造接管,计数从 61 涨到了 62。轮到 move 构造和 move 赋值的时候,计数是不动的,句柄本身是没挪窝的,动的只是咱们这边的包装纸。release 之后的计数同样不动,所有权交出来了,句柄还是开着的,直到咱们手动 CloseHandle 才落回 61。reset(新句柄)涨了一位,reset() 又立即落了回去。等作用域结束了,析构把该关的都关了,精确地回到 baseline。61 这个绝对值是随进程环境浮动的,您跑的时候多半不是它,您只要对得上这套涨落规律,就算过关了。

最后咱们故意把 NULL 陷阱踩一次给您看。CreateEventW 撞名的时候失败返回的是 NULL,咱们把这个失败值直接塞给 unique_handle:

```text
[4] 陷阱:把 CreateEventW 的失败值 NULL 直接塞给 unique_handle
  bool(bad)=1  —— NULL != INVALID_HANDLE_VALUE,-1 哨兵把它判成了"有效"
  析构里 CloseHandle(NULL) 留下 err=6(6=ERROR_INVALID_HANDLE,无害但语义错)
```

`bool(bad)=1`,一个失败值被 RAII 类判成了有效,析构的时候还对着 NULL 认认真真调了一次 CloseHandle。程序倒是不炸,只留下了 err=6,但语义全错了。这提醒咱们一件事:unique_handle 只认 -1 这一套哨兵,NULL 家族的 API,判错必须发生在装进 RAII 的那一步之前,这正是 check_win32 存在的理由。

## 归还语义:release/reset/swap

骨架管住了“忘了还”的这半个问题,而另外半个交给 release、reset、swap,管所有权的主动出手。咱们用 /dev/null 当实验品(open 永远成功又不占盘),strace 在旁边做的全程记录:

```text
$ ./release_reset_swap
== 1) release(): give up ownership ==
[open     ] a -> fd 3
[release  ] a gave up fd 3, a now holds nothing
[manual   ] ::close(3) done — the only close it gets
== 2) reset(new_fd): close old, take new ==
[open     ] b -> fd 3
[open     ] c -> fd 4
[reset(new)] b closed its old fd and now holds 4
[reset()  ] b.reset() -> b now holds nothing
== 3) swap(): exchange roles ==
[open     ] x -> fd 3
[open     ] y -> fd 4
[swap     ] x holds 4, y holds 3
== done ==
```

`release()` 做的是把所有权还给调用方:对象交出了 fd、自己也变空了,此后的析构不再碰它。它倒不是撒手不管——fd 好好地开着,只是它的归属从对象换到了您手上——真实的用途全在移交:dup2 重定向做完了,咱们把原 fd 交给别人收尾,或者把 fd 递给一个只认裸 int 的 C 接口。移交出去之后析构自然就不碰它了,strace 里是可以作证的,那个 fd 全程只有咱们手动的那一次 close:

```text
openat(AT_FDCWD, "/dev/null", O_RDONLY) = 3
close(3)                                = 0      <- release 之后调用方手动的唯一一次
```

`reset` 是双向的。带参的调用会把手里现有的 close 掉,再接管新的 fd,strace 里 close(3) 就落在 b 拿到 4 的路上。无参的调用就是单纯的关闭加变空,close(4) 之后 b 就空手了。`swap` 换的是所有权而不是编号:x 和 y 各持一个 fd,换完了就成了 x holds 4、y holds 3,fd 的编号跟着所有权走、不跟变量名走。swap 之后的收尾,咱们直接看存档 strace 里的那两行:

```text
close(3)                                = 0      <- y 声明得晚、析构得早,它手里的 3 被关掉
close(4)                                = 0      <- 轮到 x,给它手里的 4 收尾
```

此刻的 3 在 y 手里,y 是后声明的、析构也来得早——栈上对象销毁的顺序永远与声明相反,这两行 close 的排列,正是规则落在 strace 上的样子。mapped_region 那边的同三个函数形态一致,release 交还的是地址加长度两样,咱们上面看过 released 结构体了。

## 进容器:vector 里的 unique_fd

move-only 类型最常去的地方是容器。咱们把 unique_fd 塞进 `std::vector`,让实验替咱们把几件事数清楚:扩容要搬几次,shuffle 和 sort 的搬运各有几次,作用域结束了还漏不漏。计数靠的是一个观测子类,咱们在 unique_fd 上叠一个移动构造的计数,资源语义留在了基类:

```cpp
// 计数用的观测子类:叠加一个移动构造计数,资源语义全在基类
class counted_fd : public unique_fd
{
public:
    explicit counted_fd(int fd = -1) noexcept : unique_fd(fd) {}

    counted_fd(counted_fd&& other) RAII_MOVE_NOEXCEPT
        : unique_fd(std::move(other))
    {
        ++s_moves;
    }
    // ...移动赋值、swap、静态计数器略

private:
    static int s_moves;
};
```

`RAII_MOVE_NOEXCEPT` 是个实验的开关:默认构建出来的它是 `noexcept`,加上 `-DRAII_MOVE_MAY_THROW` 再编出来的那一版,它就是裸的、可能抛的移动构造。两份程序咱们各跑一遍:

```text
$ ./vector_noexcept
counted_fd: copy_constructible=0 nothrow_move_constructible=1 (RAII_MOVE_MAY_THROW not defined)
== A) emplace_back x 8, no reserve ==
   size=8 capacity=8 moves=7
   fds (8): 3 4 5 6 7 8 9 10
== B) shuffle (mt19937, fixed seed) ==
   fds (8): 3 7 6 8 5 4 10 9
   moves during shuffle=0 (swap moves no bytes)
== C) sort by fd ==
   fds (8): 3 4 5 6 7 8 9 10
   moves during sort=7
   fd multiset unchanged: yes
== D) scope end: vector destructs, 8 fds close ==
fds: before=3 after=3 (back to baseline: yes)
== E) reserve(8) first, then emplace_back x 8 ==
   size=8 capacity=8 moves=0
   fds (8): 3 4 5 6 7 8 9 10
== done ==
```

A 段咱们不 reserve 直接塞 8 个,capacity 走的是 1、2、4、8 三次翻倍,每次翻倍搬的都是旧元素,凑出来的 1+2+4 正好是 7 次移动构造。搬的只是包装:fd 编号 3 到 10 始终没变,而内核那边一次 close 都没发生,动的是“哪个对象持有它”的这一层记录。咱们再看 E 段,reserve(8) 之后咱们同样塞 8 个,搬迁的次数直接归了零,容量也一步到位了,连那 7 次都省了。

B 和 C 的对比,笔者特别喜欢。shuffle 之后 fd 的顺序真的乱了(3 7 6 8 5 4 10 9),移动构造却是零次:std::shuffle 走的是元素级的 swap,而咱们的 swap 只交换两个 int、一个字节都不搬。轮到 sort 的时候,7 次移动构造就实打实地发生了,fd 的集合却纹丝不动,multiset unchanged 的核对也是 yes。所以算法上的搬运与扩容的搬运是两码事,答案跟着算法的实现走,而不跟着直觉走。咱们看 D 段:作用域结束了,vector 的析构顺带把 8 个元素逐个析构,fd 数也就回了基线、一个都没漏。

然后是那个流传很广的说法:移动构造不标 noexcept,vector 扩容的时候就会退回拷贝。这话对可拷贝的类型是成立的,它的机制就是 `std::move_if_noexcept`:移动可能抛、又拷得起的时候它就用拷贝,拷坏了可以整体扔掉,原来的 vector 完好无损。可是对咱们这一类拷贝被删掉的类型,它就算想退也没了退路,标准里的 move_if_noexcept 只在类型可拷贝的时候才交出左值引用,move-only 类型拿到的始终是右值。实测也作证了,throwing 版的输出除了第一行 banner,往下每一行都与 noexcept 版逐字段地一致,7/0/7/0 的计数也一处都不差:

```text
$ ./vector_throwing
counted_fd: copy_constructible=0 nothrow_move_constructible=0 (RAII_MOVE_MAY_THROW defined)
（其余输出与 noexcept 版逐字段相同:moves 7 / 0 / 7 / 0）
```

那 noexcept 白标了?也不是。非 noexcept 真正损失的是 vector 的强异常保证:扩容搬到一半移动构造抛了,新旧两块缓冲谁都不完整了,数据结构回不到原样了。资源倒是不会漏的,栈展开时的析构照常把每个元素关掉,烂掉的只是容器这一层的完整性。所以咱们照旧给移动构造标 noexcept,何况对咱们这几个类来说,noexcept 标的本来就是实话。

## 这三件工具的去向

咱们手里的 unique_fd、mapped_region、unique_handle,从本篇起是全系列的公共词汇,唯一定义的地方也落在了这儿,后面各篇咱们只引用、不再重定义。[POSIX 文件 I/O](../linux/file-io/01-posix-file-io.md) 那篇会带着它们走完 fd 的一生,[mmap 内存映射](../linux/file-io/02-mmap-memory-mapping.md) 那篇把 mapped_region 从头用到了尾。Windows 侧的 [Win32 文件 I/O](../windows/file-io/01-win32-file-io.md) 则更干脆,已经在文件复制器里把 unique_handle 用上了。您想复跑本篇的实验,开头交代过的两处存档里,README 把每条命令都写好了。

下一篇《错误处理范式:从 errno 到 expected》会把错误处理讲全:errno 与 GetLastError 的线程局部性、失败之后立刻装箱的纪律,以及双出口的约定:工具层用的是 expected,应用顶层用的是 system_error。Windows 侧 check_win32 的来龙去脉,咱们把它留在 [Win32 文件 I/O](../windows/file-io/01-win32-file-io.md) 那篇里讲。咱们写到这儿,资源忘关这件事算是有了着落。错误报不准怎么办,咱们下一篇接着办。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="close(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/close.2.html"
  />
  <ReferenceItem
    :id="2"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="3"
    title="CloseHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle"
  />
  <ReferenceItem
    :id="4"
    title="GetProcessHandleCount function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount"
  />
  <ReferenceItem
    :id="5"
    title="GetCurrentProcess function(Remarks:伪句柄与 CloseHandle)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocess"
  />
  <ReferenceItem
    :id="6"
    title="std::move_if_noexcept"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/utility/move_if_noexcept"
  />
  <ReferenceItem
    :id="7"
    title="std::unique_ptr"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr"
  />
</ReferenceCard>
