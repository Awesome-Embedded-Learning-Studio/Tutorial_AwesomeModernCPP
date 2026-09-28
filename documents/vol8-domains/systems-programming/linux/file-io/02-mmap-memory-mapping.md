---
title: "mmap 内存映射:把文件贴进地址空间"
description: mmap 把文件直接贴进地址空间,读文件变成拿指针访问内存,read 那次"页缓存到用户缓冲"的拷贝被整个省掉。本篇拆开六个参数,用 mapped_region 做 RAII 收口;再实测三件事:首次触碰的缺页比二次访问贵一个数量级、文件被 truncate 后摸越界区收到的是 SIGBUS 而不是返回值、512 MiB 顺序读 mmap 反而输给 read 循环——什么时候该用 mmap,让数字说话
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 18
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 内存管理
  - 优化
---

# mmap 内存映射:把文件贴进地址空间

上一篇咱们把 fd 的一生走通了,这一篇回头看看 `read` 的成本都花在哪儿。数据从文件到你的变量,中间隔着两层:内核把文件内容搬进**页缓存(page cache)**,`read` 再把这块内核内存**拷贝一份**到你的缓冲区。您琢磨一下:顺序读大文件的时候,第二次拷贝就是白白烧掉的内存带宽。随机访问更亏,每次都为几十字节跑一趟系统调用。mmap 给出另一条路:**让内核把文件的页直接贴进你的地址空间**,你拿指针访问,数据在页缓存里只有一份,零拷贝。

更妙的是,`mmap` 建立映射的那一刻**几乎什么都没干**:不读数据,不占内存,只在内核里登记了一句"这段地址合法,背后是那个文件"。真正的搬运,要等你第一次摸到某个页才发生,这一下叫**缺页(page fault)**。懒加载配上零拷贝,动态链接器、数据库,还有一切"把文件当数组用"的程序,地基都是这套组合。本篇咱们把六个参数一个个看过去,把映射的释放也交给 RAII,然后实测三件事:缺页到底多贵,文件在背后被截短会发生什么,以及 512 MiB 顺序读里,mmap 和 read 谁赢。代码全部在 WSL 的 `/tmp/sysprog-linux02/` 下编译运行,工具是 g++ 16.2.1,选项 `-std=c++23 -O2`。`unique_fd`、`sys_call`、`errno_code` 这几件工具,沿用上一篇的定义。

## mmap() 参数全解:六个参数,一个返回值陷阱

```c
void *mmap(void addr[.length], size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void addr[.length], size_t length);
```

咱们从签名往下数。`addr` 基本永远传 `nullptr`,让内核自己挑一个页对齐的空闲地址。您非要传非空进去,那也只是个"建议",内核可以不理。`length` 是字节数,内核会按页向上取整。取整还带来一个边界现象:映射盖住文件尾的时候,末尾不满一页的那部分读出来是零,写它也不会写回文件。零填充的这些细节,咱们到下面 mapped_region 一节再看。

`prot` 从 `PROT_READ`/`PROT_WRITE`/`PROT_EXEC`/`PROT_NONE` 里挑,而且不能和 open 的模式打架。不过这个限制只管 `MAP_SHARED`:您拿一个 O_RDONLY 的 fd,配上 `PROT_WRITE` 再加 `MAP_SHARED`,内核直接回您一个 EACCES。换成 `MAP_PRIVATE` 就不受此限,只读 fd 照样能建可写的私有映射,您写的只是写时复制出来的副本。man 2 mmap 的 ERRORS 一节里,EACCES 也就只挂在 MAP_SHARED 头上。

`flags` 决定语义,咱们挨个看。**MAP_SHARED**:你的写直接落在文件的页缓存上,其他映射同一文件的进程立刻可见,内核再择机写回。**MAP_PRIVATE** 走写时复制(copy-on-write),你的修改落在私有副本上,永不落盘。所以只读 fd 配 `PROT_WRITE` 建私有映射完全合法,动态链接器给 so 做重定位,用的就是这一手。

剩下的几项,咱们走得快些,`MAP_ANONYMOUS` 和 `MAP_POPULATE` 这会儿记个名字就行:`MAP_ANONYMOUS` 是不挂文件的匿名映射,它和 malloc 的恩怨留到内存管理篇再算。`MAP_POPULATE`(内核 2.5.46 起)会预填页表并触发预读,把缺页成本挪到 mmap 调用这一刻。`fd` 就是上一篇 open 出来的那个,man 2 mmap 明说,映射建立后马上 close(fd) 也不影响映射。不过我们还是让 `unique_fd` 一直挂着,省心。`offset` 必须是页大小的整数倍。页大小也别硬编码 4096,运行时用 `sysconf(_SC_PAGE_SIZE)` 拿才稳妥。

::: warning 失败时的返回值是 MAP_FAILED
mmap 失败返回 `MAP_FAILED`(即 `(void *) -1`)并设置 errno,**不是 nullptr**。`if (p)` 查不出失败,必须写成 `if (p == MAP_FAILED)`。上一篇 `sys_call` 的 `result == -1` 在这里根本编不过:mmap 返回的是指针,`void*` 不能和整数 `-1` 直接比较。失败值其实就是 `(void *) -1`,但得写成 `p == MAP_FAILED` 才是合法的比较。所以这里咱们手写判断,下面 `mapped_region` 里您会看到它。
:::

## mapped_region:本篇的 RAII 工具

和 fd 一样,映射也是"取得后必须释放"的资源,漏掉 `munmap` 就是地址空间泄漏。咱们按上一篇的思路来:`unique_fd` 管 close,这里管 munmap,还是同一副 move-only 骨架。

```cpp
class mapped_region {
public:
    mapped_region() noexcept = default;

    mapped_region(const unique_fd& fd, std::size_t length, int prot, int flags,
                  off_t offset = 0)
    {
        void* p = ::mmap(nullptr, length, prot, flags, fd.get(), offset);
        if (p == MAP_FAILED) {
            throw std::system_error{errno_code(), "mmap"}; // 返回 void*,整数 -1 检查编不过,须判 MAP_FAILED
        }
        addr_ = static_cast<unsigned char*>(p);
        const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
        length_ = (length + page - 1) / page * page; // 内核按页取整,记录真实映射长度
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
            ::munmap(addr_, length_);
            addr_ = nullptr;
            length_ = 0;
        }
    }

    unsigned char* data() const noexcept { return addr_; }
    std::size_t size() const noexcept { return length_; } // 向上取整后的映射长度
    explicit operator bool() const noexcept { return addr_ != nullptr; }

    mapped_region(const mapped_region&) = delete;
    mapped_region& operator=(const mapped_region&) = delete;

private:
    unsigned char* addr_ = nullptr;
    std::size_t length_ = 0;
};
```

请您留意 `size()`:它返回的是**向上取整后**的映射长度,不是您传进去的 length。munmap 要用同一个长度,所以我们干脆自己算好记下。取整多出来的尾巴里装的是什么,得看文件尾落在哪儿,而不是看取整边界。文件长度不是页的整数倍、映射又盖住文件尾的时候,从文件尾到取整边界的那一截,读出来是零,写它也不会写回文件,这就是上面预告过的零填充。反过来,length 小于文件大小、只映射前缀的时候(大文件上常见的用法),尾巴里读到的仍是真实的文件数据。

尾巴的事说完了,那要是摸出去呢?真正越出 `size()`,咱们就离开映射划定的范围了:踩到未映射的页,吃一个 SIGSEGV。隔壁要是恰好贴着别的映射,连信号都没有,您读到的是别人的数据,这更糟。SIGBUS 是另一回事,它管的是映射里的页整页落在文件尾之外,或者文件在背后被截短,咱们到 SIGBUS 一节实测它。

## 缺页:映射建立时,其实什么都没发生

mmap 返回那一刻,进程只多了一段 VMA(虚拟内存区域)描述,一页数据都没动。第一趟摸过去,每碰一个还没映射的页,CPU 就陷入内核:内核查页缓存,把文件那一页直接挂进你的页表。陷入内核这一下,加上把页挂进页表,这一趟下来,就是缺页成本。咱们来实测:造一个 64 MiB 文件,映射之后每页摸一个字节,连摸三趟,minor fault 计数直接读 `/proc/self/stat` 的 minflt 字段。

```cpp
// fault_cost.cpp(节选):三趟"每页摸一个字节",计时与打印的辅助函数略
mapped_region region(fd, 64u << 20, PROT_READ, MAP_PRIVATE);

fault_counts f0 = self_faults();
unsigned long s1 = touch_pages(region.data(), region.size(), page); // 第一趟,缺页
fault_counts f1 = self_faults();
unsigned long s2 = touch_pages(region.data(), region.size(), page); // 第二趟,页都在
::madvise(region.data(), region.size(), MADV_DONTNEED);             // 丢页,人造第三次首次
unsigned long s3 = touch_pages(region.data(), region.size(), page); // 缺页应声回来
```

```text
$ ./fault_cost
page size     : 4096, mapped 16384 pages
first touch   :   7.20 ms, minor faults  1028, sum 2084688
second touch  :   0.15 ms, minor faults     0, sum 2084688
after DONTNEED:   4.97 ms, minor faults  1024, sum 2084688
```

咱们把三趟数字摆开:第一趟 7.20 ms(约 0.44 µs/页),第二趟 0.15 ms(约 9 ns/页),差了快五十倍。`MADV_DONTNEED` 把页丢掉之后,耗时和缺页计数一起回来了。还有一个数字值得您多看一眼:16384 页,只产生了约 1028 次缺页。为什么这么省?内核有 fault-around 优化,处理一次缺页时,顺手把这附近约 16 页(64 KiB)的文件页一起挂上,等于按批发价进货。这也解释了 mmap 对随机访问的意义:**只为你摸过的页付钱**。read 则要么提前把整块搬进来,要么每次 syscall 碎着读。

> 咱们顺带交个底:这次文件刚写完,又躺在 tmpfs 上,缺页全是 minor,页就在缓存里。真磁盘上冷文件的第一摸是 major fault,还得等 I/O。

## SIGBUS:文件在背后被截短了

映射长度是您声明给 mmap 的,内核不会替您看住文件大小。映射建立之后,文件完全可能被截短:日志滚动、原地压缩,或者别的进程手滑一个 ftruncate,都干得出来。这时候您再去摸越界区域,man 2 mmap 的 ERRORS 一节写得直白:**Attempted access to a page of the buffer that lies beyond the end of the mapped file,信号是 SIGBUS**。摸出界的这一下,您拿不到返回值,也拿不到 errno,内核直接把 SIGBUS 信号送进进程,而它的默认处理就是直接终止进程。咱们装个 handler,复现一遍:

```cpp
// sigbus.cpp(节选):write_all/write_hex 是 write(2) 的小封装,完整程序在 WSL 编译运行
void on_sigbus(int, siginfo_t* info, void*)
{
    write_all("\n[handler] caught SIGBUS, faulting address = 0x");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    write_all(info->si_code == BUS_ADRERR ? ", si_code = BUS_ADRERR\n" : "\n");
    ::_exit(70);
}

// main 里:映射两页,摸完第一页后把文件砍到一页,再摸第二页
mapped_region region(fd, 2 * static_cast<std::size_t>(page), PROT_READ, MAP_SHARED);
struct sigaction sa {};
sa.sa_sigaction = on_sigbus;
sa.sa_flags = SA_SIGINFO;
::sigaction(SIGBUS, &sa, nullptr);

volatile unsigned char first = region.data()[0];   // 页内,安全
sys_call("ftruncate", ::ftruncate, fd.get(), page); // 文件砍半,映射毫不知情
volatile unsigned char second = region.data()[page]; // 越界,SIGBUS 在此爆发
```

```text
$ ./sigbus; echo "exit code: $?"
mapped 2 pages (8192 bytes) at 0x79b28a8f8000
touch +0        ... ok
ftruncate -> 4096 bytes, mapping still claims 8192
touch +4096     ... beyond EOF

[handler] caught SIGBUS, faulting address = 0x000079b28a8f9000, si_code = BUS_ADRERR
[handler] signal, not a return value; _exit(70)
exit code: 70
```

咱们对着输出看一眼:0x79b28a8f9000 正好是基址加 4096,节选没贴出的后半段里,还有一行 `NOT REACHED` 打印,它永远没机会执行。`si_code` 是 `BUS_ADRERR`,内核在说"这个地址背后的对象没了"。

::: warning SIGBUS 是 mmap 最出名的翻车点
handler 里只能用 `write`、`_exit` 这类异步信号安全的函数,`printf` 都不行,它可能正持有锁,一不留神就死锁给您看。工程上的防御,咱们就三招。头一招,映射前 `fstat` 量好大小。第二招,length 是自己算的就要自己兜底,比如"文件头里声明的长度"可能是错的。第三招,和别的进程共享文件时,约好谁都不许 truncate。
:::

## msync 与 madvise:回写时点与给内核递话

MAP_SHARED 写完之后,您改的是页缓存里的页,内核总有一天会写回,但"哪一天"不受您控制。`msync(addr, len, flags)` 给咱们三个选择。**MS_SYNC** 同步写回并等待完成,咱们想控制落盘时点,就拿它。**MS_ASYNC** 本意是"排队,别等",但 man 2 msync 明说,Linux 2.6.19 起 MS_ASYNC 实际上是 no-op,因为内核自己跟踪脏页、按需冲刷。**MS_INVALIDATE** 让其他映射着旧数据的进程失效,逼它们重新读。这一套 POSIX.1-2024 也收录了,不挑内核。

`madvise(addr, len, advice)` 不改语义,咱们只是给内核递一句访问模式。**MADV_SEQUENTIAL** 是告诉内核"我要从头扫到尾",内核会激进预读、读完即弃。**MADV_RANDOM** 正相反,"别替我预读",免得预读白搬一堆用不上的页。**MADV_DONTNEED** 把页丢掉,丢完之后三种映射各有各的下场:共享映射再访问,会从文件重新填充。私有文件映射丢掉的是写时复制出来的副本。匿名映射丢了才变成清零页。缺页一节实测的第三趟,它已经露过一手了。

## MAP_PRIVATE:写时复制,眼见为实

MAP_PRIVATE 的约定是"你改你的,文件不知道"。空口无凭,咱们让同一个进程开两个映射当面对质:私有映射上改 4 字节,`pread` 直接问文件,再看新开的共享映射能看到什么。最后在共享映射上改 4 字节,配一次 `msync(MS_SYNC)` 做对照。

```cpp
// cow.cpp(节选):peek_file() 是 pread 16 字节的小封装
mapped_region priv(fd, 16, PROT_READ | PROT_WRITE, MAP_PRIVATE);
std::memcpy(priv.data(), "XXXX", 4); // 写的是 COW 出来的私有副本
std::printf("file via pread   : %s\n", peek_file(fd).c_str()); // 文件纹丝不动

mapped_region shared(fd, 16, PROT_READ | PROT_WRITE, MAP_SHARED);
std::printf("MAP_SHARED view  : %.16s\n", reinterpret_cast<const char*>(shared.data()));

std::memcpy(shared.data() + 8, "YYYY", 4); // SHARED:直接改页缓存
sys_call("msync", ::msync, shared.data(), shared.size(), MS_SYNC); // 落盘
std::printf("after msync file : %s\n", peek_file(fd).c_str());
```

```text
$ ./cow
MAP_PRIVATE view : XXXX...(just wrote)
file via pread   : AAAABBBBCCCCDDDD
MAP_SHARED view  : AAAABBBBCCCCDDDD (sees the ORIGINAL)
after msync file : AAAABBBBYYYYDDDD
```

结果摆在这儿,您看:私有映射里明明写着 XXXX,`pread` 问回来还是 AAAA,新开的共享映射看到的也是原件。您的修改只活在私有副本里,不落盘、不污染页缓存,别的进程谁也看不见。共享映射那边写完再加 `msync(MS_SYNC)`,YYYY 立刻出现在文件里。

## 512 MiB 顺序读:mmap 没赢

开篇欠下的那场对比,现在兑现:512 MiB 文件(dd 生成),read 一口 1 MiB 循环读,mmap 整段求和,两边算同一个校验和。笔者的环境是 WSL2,内核 6.18.33.2-microsoft-standard-WSL2,CPU 是 AMD Ryzen 7 9700X,/tmp 是 tmpfs。文件常驻页缓存,磁盘这个变量被排除在外,咱们量的是纯 CPU 侧的成本。

```cpp
// bench.cpp(节选):求和循环 sum_bytes 与计时打印略
if (mode == "read") {
    ::lseek(fd.get(), 0, SEEK_SET); // 前面量长度时 lseek 到了末尾,读之前回到开头
    std::vector<unsigned char> buf(1u << 20); // 1 MiB 缓冲
    for (;;) {
        ssize_t n = ::read(fd.get(), buf.data(), buf.size()); // 页缓存→用户缓冲,一次拷贝
        if (n == 0) {
            break;
        }
        sum += sum_bytes(buf.data(), static_cast<std::size_t>(n));
    }
} else {
    const int extra = (mode == "populate") ? MAP_POPULATE : 0;
    mapped_region region(fd, len, PROT_READ, MAP_PRIVATE | extra);
    sum = sum_bytes(region.data(), region.size()); // 首摸每页缺页,零拷贝
}
```

```text
$ for i in 1 2 3; do ./bench read big.bin; ./bench mmap big.bin; done
read     :   98.1 ms, 5218.9 MiB/s, sum 68449008524, minor faults 413
mmap     :   98.7 ms, 5188.1 MiB/s, sum 68449008524, minor faults 8349
read     :   75.8 ms, 6752.9 MiB/s, sum 68449008524, minor faults 413
mmap     :   89.1 ms, 5749.0 MiB/s, sum 68449008524, minor faults 8348
read     :   77.2 ms, 6627.8 MiB/s, sum 68449008524, minor faults 415
mmap     :   90.3 ms, 5670.2 MiB/s, sum 68449008524, minor faults 8347
$ ./bench populate big.bin
mmap(MAP_POPULATE) itself took 37.0 ms
populate :   92.5 ms, 5534.9 MiB/s, sum 68449008524, minor faults 8350
```

第一轮算热身(频率爬坡、缓存预热),咱们看后两轮的稳态:read 稳定在 76~77 ms,mmap 稳定在 89~90 ms,**顺序读 read 赢了约 15%**。fault 计数把原因交代得明明白白:read 只有 413 次 minor fault(程序与缓冲本身那点),mmap 有 8349 次。512 MiB 被 fault-around 批发成约 8000 次缺页,省下的那次 512 MiB 拷贝,抵不过八千多次缺页加页表/TLB 的折腾。MAP_POPULATE 也没翻盘:37 ms 挪进 mmap 调用,总时间 92.5 ms,还略亏一点。您也别拿系统调用说事:512 次 read,按[总纲](../../00-overview.md)实测的约 125 ns/次,总共 64 µs,根本不是瓶颈。

那 mmap 什么时候赢?随机访问大文件的时候,只碰要的页,read 要么预读浪费、要么 syscall 太碎。多进程共享同一份文件映射,也是 mmap 的主场,动态链接器加载 so 就是教科书案例,一份物理页全员共享。还有把文件当内存里的结构直接用,或者写路径要零拷贝。什么时候用 read?纯顺序流,预读已经是为你优化的。小文件,要跨平台一致行为,或者干脆不想伺候 SIGBUS 的,也归它。落到咱们手上:整段顺序扫的,交给 read。要来回摸的,再请 mmap。

## 另一侧怎么看

Windows 没有 mmap,同一件事,咱们得分成两步走:`CreateFileMapping` 造一个"映射对象",拿着文件 HANDLE 和页保护属性,相当于把 fd 和 prot 提前登记好。再由 `MapViewOfFile` 把它贴进本进程的地址空间,这才是 mmap 的对应物。您可以把 prot 理解成这边的 PAGE_READONLY,或者 PAGE_READWRITE。MAP_PRIVATE 的镜像叫 FILE_MAP_COPY,同样是写时复制。msync 的对应物是 FlushViewOfFile,不过单用它只发起脏页写回、不等落盘,想要 MS_SYNC 那样等到底的语义,咱们还得再补一个 FlushFileBuffers。最要当心的差异在这儿:Linux 的 SIGBUS 剧本,也就是文件被截短后再摸越界区,在 Windows 根本拍不成,截短那一步就被 SetEndOfFile 拦下了。Windows 送上来的是 ACCESS_VIOLATION 结构化异常,SIGBUS 的表亲,它接手的是写只读视图这类权限越界。至于映射视图底下的 I/O 出了错,Windows 送来的则是另一位表亲,EXCEPTION_IN_PAGE_ERROR。展开的实现,见镜像篇:[文件映射:CreateFileMapping 与 MapViewOfFile](../../windows/file-io/02-file-mapping.md)。

## 小结

这一篇咱们实测确认下来的东西,收在这里,方便您回头查:

- mmap 六参数:`addr` 传 nullptr、`length` 内核按页取整、`prot` 别和 open 冲突(`MAP_SHARED`+`PROT_WRITE` 要求 fd 可写)、`MAP_SHARED`/`MAP_PRIVATE` 定语义、`offset` 必须 `sysconf(_SC_PAGE_SIZE)` 的整数倍、`fd` 映射后即可关
- 失败返回 `MAP_FAILED` 不是 nullptr。`mapped_region` 用 RAII 管起来,move-only,取整后的真实长度,咱们认 `size()` 报的这份就行
- 映射建立零成本,首次触碰才缺页:实测首摸约 0.44 µs/页,二次访问约 9 ns/页。fault-around 让 16384 页只花约 1028 次缺页
- 文件被 truncate 后摸越界区收到 SIGBUS 信号而非错误码。handler 只能异步信号安全。防御靠 fstat 和长度兜底
- MS_SYNC 控制落盘时点,MS_ASYNC 在 Linux 2.6.19+ 实际是 no-op。madvise 递访问模式,DONTNEED 丢页
- MAP_PRIVATE 写时复制不落盘(实测 XXXX 永远写不进文件)。512 MiB 顺序读实测 read 快约 15%,mmap 赢在随机访问、共享与零拷贝

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="2"
    title="msync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/msync.2.html"
  />
  <ReferenceItem
    :id="3"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="4"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
