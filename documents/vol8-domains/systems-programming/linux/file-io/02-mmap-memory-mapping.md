---
title: "mmap 内存映射:把文件贴进地址空间"
description: mmap 把文件直接贴进地址空间,读文件变成拿指针访问内存,read 那次从页缓存到用户缓冲的拷贝被整个省掉;本篇把六个参数与行为后果逐个对上,用 mapped_region 做 RAII 管理,实验从缺页单价、/proc/self/maps、SIGBUS、mprotect 两类 si_code、跨进程可见、Dirty 与写时复制,一路陪到 512 MiB 顺序读——两台机器给出了相反的胜负
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 32
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

上一篇咱们把 fd 的一生走通了,这一篇回头看看 `read` 的成本都花在了哪儿。您要的数据从文件一路走到变量,中间隔着的是两道搬运:内核把文件内容搬进了**页缓存(page cache)**,`read` 再把这块内核内存往您那儿拷一份。您琢磨一下,顺序读大文件的时候,第二次拷贝就是白白烧掉的内存带宽,随机访问就更亏了,您每摸几十字节,就免不了跑一趟系统调用。mmap 给出了另一条路:**让内核把文件的页直接贴进您的地址空间**,您拿指针访问的就是页缓存本身,拷贝的活儿全省了。

更妙的是,`mmap` 建立映射的那一刻**几乎什么都没干**:一页数据都没读,一字节的内存都没占,只在内核里登记了一句"这段地址合法,背后是那个文件"。真正的搬运,要等您第一次摸到某个页才发生,这一下就叫**缺页(page fault)**了。懒加载配上零拷贝的组合,养活了动态链接器与数据库,也养活了一切"把文件当数组用"的程序,打的都是同一套地基。本篇咱们把六个参数一个个看过去,把映射的释放交给 RAII,然后排一串实验往下走:缺页有多贵、文件在背后被截短会发生什么、两个进程怎么共享同一份文件,一路到 512 MiB 顺序读里 mmap 与 read 的胜负。

实验环境咱们交代清楚,后面的数字都要靠它对表:第一批实验(缺页、SIGBUS、写时复制)出自笔者的台机,用的 CPU 是 AMD Ryzen 7 9700X,这一批的代码没有留档,咱们只在文中留了数字。后来补做的 mprotect、`/proc/self/maps`、跨进程可见性与 Dirty 计数,加上第一批三个实验在笔记本上的复跑,换的是一台 i7-13700H 的机器,代码与全部原始输出收进了仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/02-mmap-memory-mapping/`。512 MiB 基准则是两台各跑了一轮,哪轮是哪台的数字,咱们到基准一节再细说。两边都是 WSL2 的环境,内核都是 6.18.33.2-microsoft-standard-WSL2 的同一构建,g++ 也都是 16.2.1 的同一版本。出自哪台机器的数字,咱们行文里随用随标。`unique_fd`、`sys_call`、`errno_code` 这三件工具的用法,咱们沿用上一篇的定义。

## mmap() 的六个参数:每一个都挂着行为后果

```c
void *mmap(void addr[.length], size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void addr[.length], size_t length);
```

咱们从签名往下数。`addr` 基本上永远传的是 `nullptr`,让内核自己挑一个页对齐的空闲地址,man 2 mmap 说这是最可移植的做法。您非要传非空进去,它到了内核的手里也只是个建议:不低于 `/proc/sys/vm/mmap_min_addr` 的前提下,内核会挑附近的页边界尝试放置,那儿已经有映射的话,它就另挑一个了,挑出来的地址可能与您的建议毫无关系。

能把 `addr` 从建议升格成命令的,是 `MAP_FIXED`:映射会精确落在您给的地址上,要是目标区间与既有映射重叠了,重叠的部分会被**直接丢弃**,连一声通知都不给您。man 页的 NOTES 自己都承认,这个 flag 唯一安全的用法,是落在此前已经预留好的地址区间上,多线程的程序要是乱用它,随手把 libc 或者某个线程栈的映射拆掉,都不会有人提醒您一声。Linux 4.17 起有了收敛很多的 `MAP_FIXED_NOREPLACE`:它同样强制地址的落点,但绝不覆盖既有的映射,区间被人占了就失败,errno 回的是 `EEXIST`。两个线程同时抢同一段地址的话,赢的拿到,输的收到 EEXIST。老内核不认识这个 flag 的时候,它会退化成普通的建议语义、返回另一个地址,所以咱们拿到返回值,都要和请求的地址对一遍才放心。咱们顺带提名一位亲戚:`mremap` 能原地扩展、收缩甚至搬家一段已有的映射,可惜 POSIX 里没有它的位置,咱们留给内存管理篇再去打交道。

`length` 说的是字节数,内核会替您按页向上取整。取整还带来一个边界现象:映射盖住文件尾的时候,末尾不满一页的那部分读出来是零,写了它也不会回写文件。零填充的这些细节,还有它一个挺反直觉的例外,咱们到下面 mapped_region 一节再看。

`prot` 就从 `PROT_READ`/`PROT_WRITE`/`PROT_EXEC`/`PROT_NONE` 里挑好的那档,而且不能和 open 的模式打架。不过这个限制只管 `MAP_SHARED`:您给一个 O_RDONLY 的 fd 配上 `PROT_WRITE` 加 `MAP_SHARED`,内核直接回您一个 EACCES。换成 `MAP_PRIVATE` 就不吃这一限了,只读 fd 照样能建可写的私有映射,您写的只是写时复制出来的副本。

这里得把上一篇欠的精确性补上:man 2 mmap 的 ERRORS 里,EACCES 并非只挂在 `MAP_SHARED` 一家的头上,它的完整条件是四选一:fd 指向了非普通文件,或者您要文件映射但 fd 没以读方式打开,或者 `MAP_SHARED` 配 `PROT_WRITE` 而 fd 不是 O_RDWR,或者文件是 append-only 而您请求了 `PROT_WRITE`。咱们关心的"只读 fd 想写"只中了第三条,实测就排在 mprotect 一节的第 5 步。

`flags` 决定的是语义,咱们挨个看。**MAP_SHARED**:您的写直接落在文件的页缓存上,其他映射同一文件的进程立刻可见,写回的时机由内核挑。**MAP_PRIVATE** 走的是写时复制(copy-on-write),您的修改落在私有副本上,永远进不了文件。所以只读 fd 配 `PROT_WRITE` 建私有映射完全合法,动态链接器给 so 做的重定位,用的就是这一手。

剩下的几项咱们走得快些,`MAP_ANONYMOUS` 和 `MAP_POPULATE` 这会儿记个名字就行:`MAP_ANONYMOUS` 是不挂文件的匿名映射,它和 malloc 的恩怨留到内存管理篇再算。`MAP_POPULATE`(内核 2.5.46 起)会替您预填页表、触发预读,把缺页的成本挪到 mmap 调用的这一刻。`fd` 就是上一篇 open 出来的那个,man 2 mmap 也明说了,映射建立之后马上 close(fd) 也不影响既有的映射。不过咱们还是让 `unique_fd` 一直挂着,图的是省心。`offset` 必须是页大小的整数倍。页大小您也别硬编码 4096,运行时拿 `sysconf(_SC_PAGE_SIZE)` 问出来的才稳妥。

::: warning 失败时的返回值是 MAP_FAILED
mmap 失败时返回的是 `MAP_FAILED`(即 `(void *) -1`)并设置 errno,**而不是 nullptr**。用 `if (p)` 是查不出失败的,咱们必须写成 `if (p == MAP_FAILED)`。上一篇 `sys_call` 的 `result == -1` 在这里根本编不过:mmap 返回的是指针,`void*` 您不能拿它和整数 `-1` 直接比较。所以这里咱们手写判断,下面 `mapped_region` 里您会看到它。
:::

## mapped_region:把 munmap 写进析构函数

映射跟 fd 属于同一类的资源,咱们拿到手就得释放,漏掉 `munmap` 就是地址空间的泄漏。上一篇 `unique_fd` 怎么把 close 写进析构函数,咱们已经演过一遍了,这里的骨架倒是一模一样,move-only 的做法、析构走 reset,只是标的从 fd 换成了一段地址。需要您多看两眼的地方,咱们放在代码后面讲。

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

请您留意 `size()`:它返回的是**向上取整后**的映射长度,不是您传进去的 length。munmap 要用的也是同一个长度,所以咱们干脆自己算好记下,这就是构造函数里那行除法的用意。取整多出来的尾巴里装的是什么,得看文件尾落在了哪儿,而不是看取整边界。文件长度不是页的整数倍、映射又盖住文件尾的时候,从文件尾到取整边界的那一截,读出来的全是零,写了它,文件也是不会知道的,这就是上面预告过的零填充。咱们反过来看,length 小于文件大小、只映射前缀的时候(大文件上常见的用法),尾巴里读到的仍是真实的文件数据。

零填充还有一段 man 页 BUGS 一节的告诫,咱们得补在后面:您写到 EOF 之后的那半页,数据虽然永远进不了文件,却偏偏会**残留在页缓存里**,同文件的后续映射可能看到您改过的内容。有的情况下,咱们把 `msync` 走在 munmap 前头,这批残留是能刷掉的,但 tmpfs 上就救不回来了。您要是拿"写了白写"去做缓存可见性的假设,翻车的就是您自己的推理。

尾巴的事说完了,那要是摸出去呢?真正越出 `size()`,咱们就离开映射划定的范围了:踩到未映射的页,内核会给您一个 SIGSEGV。隔壁要是恰好贴着别的映射,连个信号都不给您,您读到的是别人的数据,这样的情况反而更糟——凭什么越界了反而没信号?咱们把这个疑问记下。SIGBUS 管的则是另一回事:映射里的页整页落在文件尾之外,或者文件在背后被截短了,咱们到 SIGBUS 一节实测它。

## 缺页:映射建立的那一刻,什么都没发生

mmap 返回的那一刻,进程只多了一段 **VMA**(virtual memory area,虚拟内存区域)的登记、一页数据都没动。CPU 每次访存的时候,内核都拿地址去查登记表:地址落在某个 VMA 里、权限也够,就把对应的页挂进您的页表。地址不在任何 VMA 里,才轮到 SIGSEGV 出场的份。上一节留的那个疑问——为什么摸出映射范围、隔壁贴着别的映射时,连信号都没有——答案就在这儿:内核只认地址落在哪个 VMA 里,它并不记得您传给 mmap 的 length。指针走出您登记的区间、落进邻居的 VMA,那次访问在内核眼里是完全合法的,您读到的自然就是别人的数据了。

缺页本身也分了两类,分类的标准就一条:这一下要不要动磁盘。要等 I/O 把页从盘上读进来的,咱们叫它 **major fault**,这样的一下代价昂贵。不用等盘、页已经在内存里的,咱们叫它 **minor fault**,软缺页是它的小名。内核是按进程分开计数的,`/proc/self/stat` 的第 10 个字段就是 minflt,proc_pid_stat(5) 把字段号都编好了,咱们下面的实验读的正是它。实验文件是躺在页缓存里的,咱们量到的是纯缺页处理的价,真磁盘上冷文件的第一摸是 major fault,还得再等盘的 I/O。

咱们来实测:造一个 64 MiB 的文件,映射建好之后咱们每页摸一个字节、连摸三趟,minor fault 的计数直接读 `/proc/self/stat`。

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

咱们把三趟数字摆开:第一趟 7.20 ms(约 0.44 µs/页),第二趟的 0.15 ms(约 9 ns/页),差了快五十倍。`MADV_DONTNEED` 把页丢掉了之后,耗时和缺页计数一起回来了。还有一个数字值得您多看一眼:16384 页,只产生了约 1028 次缺页。为什么这么省?内核有 fault-around 的优化:处理一次缺页的时候,顺手把这附近约 16 页(64 KiB)的文件页一起挂上,等于按批发价进的货。这个 16 也不是拍脑袋来的,内核 mm/memory.c 里的 `fault_around_bytes` 旋钮写的就是 64 KiB。这也解释了 mmap 对随机访问的意义:**只为您摸过的页付钱**。read 则要么提前把整块搬进来,要么每次 syscall 碎着读。

> 这段输出出自笔者的台机。笔者后来在笔记本(i7-13700H)上又把同一个程序跑了几遍,首摸的 1.42 ms、二次的 0.25 ms,缺页计数咱们在笔记本上每遍数出来都是 1027/0/1024,台机的首摸则是 1028。绝对时长差了五倍,首摸明显更贵的这件事,两台机器都点头了。还有一件顺路的工具值得记下:`mincore(2)` 能查询一段虚拟内存里哪些页真的驻留在物理内存,咱们做缺页与驻留的对照实验时,它是天然的观测仪器,咱们内存管理篇里见。

## 用 /proc/self/maps 看见映射

VMA 听着像内核的私事,其实它整个摊开在一个文件里:`/proc/self/maps`,每个映射都有自己的一行,读它就像读普通的文本文件。咱们写个程序,给同一个文件建出的两个视图,一个 `MAP_SHARED` 从偏移 0 的位置起,另一个 `MAP_PRIVATE` 从偏移 8 KiB 的位置起,再补一个真盘 ext4 文件的视图做对照,然后咱们按路径过滤,只打印属于咱们的那几行:

```cpp
// e3.cpp(节选):read_maps 用 ifstream 逐行读 /proc/self/maps,
// print_matching 按路径子串过滤打印,单线程进程,maps 静止可放心逐行读
mapped_region shared(fd, 2 * page, PROT_READ | PROT_WRITE, MAP_SHARED, 0);
mapped_region priv(fd, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                   static_cast<off_t>(2 * page)); // 偏移 8 KiB

auto before = read_maps();
std::print("maps lines total: {} (before any mmap of the file)\n", before.size());

auto after = read_maps();
std::print("maps lines total: {} (after 3 mmaps, +{})\n", after.size(),
           after.size() - before.size());
print_matching(after, "e3data.bin", "  file-related: ");
```

```text
$ ./e3
maps lines total: 41 (before any mmap of the file)
mapped SHARED 2 pages at 0x77d7b60db000 (offset 0)
mapped PRIVATE 2 pages at 0x77d7b60d9000 (offset 8K)
maps lines total: 44 (after 3 mmaps, +3)
  file-related: 77d7b60d9000-77d7b60db000 rw-p 00002000 00:49 4657                       /tmp/l02_exps/e3_proc_maps/e3data.bin
  file-related: 77d7b60db000-77d7b60dd000 rw-s 00000000 00:49 4657                       /tmp/l02_exps/e3_proc_maps/e3data.bin
  ext4-backed : 77d7b60d7000-77d7b60d9000 r--p 00000000 08:30 660959                     /home/charliechen/l02_scratch/e3ext.bin

[same file, anonymous/bss side of the world]
  heap  : 5d798897e000-5d79889b1000 rw-p 00000000 00:00 0                          [heap]
  stack : 7ffff688f000-7ffff68b1000 rw-p 00000000 00:00 0                          [stack]

after munmap(SHARED): maps lines total: 43 (-1), SHARED line gone, PRIVATE stays
  file-related: 77d7b60d9000-77d7b60db000 rw-p 00002000 00:49 4657                       /tmp/l02_exps/e3_proc_maps/e3data.bin
```

每行的六个列,咱们拿两条 file-related 的行当标本:**地址区间**、**权限**、**文件内偏移**,剩下的 **设备号**、**inode**、**路径**。两行对应的是同一个文件,所以 inode 同为 4657、设备同是 00:49。差别只有两处:第 3 列的偏移,一行写的是 00002000、另一行写的是 00000000,正是咱们传给 mmap 的 offset。第 2 列的结尾,一行落的是 p、另一行落的是 s。perms 列的前三位是 r/w/x,第四位的 s 表示 shared、p 表示 private(写时复制),正好对应的是 `MAP_SHARED` 与 `MAP_PRIVATE`。设备号列也有它的讲究:ext4 的对照行写着 08:30,那是十六进制的写法,换算过来是十进制的 8:48,正是 `/dev/sdd` 的设备号。tmpfs 的 00:49 则是它自己的匿名块设备号,没有块设备的含义。至于 `[heap]` 和 `[stack]` 的行,设备号干脆是 00:00、inode 是 0——没挂任何文件的地界。行数的核对也请您看一眼:总数从 41 涨到了 44,三次 mmap 恰好加了三行。munmap 只走了一次,精确减掉的也是一行。总行数本身会随运行库和环境变量变的,您跑的时候多半不是 41,咱们别拿它当常数。

> 还有一个小观察留给您:映射建好之后您把底层文件删掉,这一行的路径末尾会挂上 `(deleted)` 后缀,映射本身倒是照样能用。SIGBUS 一节的实验要是把"截短"换成了"删除",maps 上显示的就是这个样子了。

## SIGBUS:文件在背后被截短了

映射长度是您声明给 mmap 的,内核不会替您看住文件的大小。映射建立之后的文件,完全可能被人截短:日志滚动、原地压缩、别的进程手滑一个 ftruncate,现实里这些都是发生过的。这时候您再去摸越界区域,man 2 mmap 的 ERRORS 一节写得直白:**Attempted access to a page of the buffer that lies beyond the end of the mapped file,信号给的是 SIGBUS**。摸出界的这一下,您拿不到返回值,也拿不到什么 errno,内核会直接把 SIGBUS 信号送进您的进程,而它的默认处理就是直接终止进程。咱们装个 handler 来复现一遍:

```cpp
// sigbus.cpp(节选):write_all/write_hex 是 write(2) 的小封装,完整程序在 WSL 编译运行
void on_sigbus(int, siginfo_t* info, void*)
{
    write_all("\n[handler] caught SIGBUS, faulting address = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr)); // write_hex 自带 0x 前缀
    write_all(info->si_code == BUS_ADRERR ? ", si_code = BUS_ADRERR\n" : "\n");
    write_all("[handler] signal, not a return value; _exit(70)\n");
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
touch +4096      ... beyond EOF

[handler] caught SIGBUS, faulting address = 0x000079b28a8f9000, si_code = BUS_ADRERR
[handler] signal, not a return value; _exit(70)
exit code: 70
```

咱们对着输出看一眼:0x79b28a8f9000 正好是基址加 4096,节选没贴出的后半段里,还有一行 `NOT REACHED` 的打印,它永远没机会执行了。`si_code` 给的是 `BUS_ADRERR`,内核在说的就是"这个地址背后的对象没了"。

::: warning SIGBUS 是 mmap 最出名的翻车点
handler 里只能用 `write`、`_exit` 这类异步信号安全的函数,`printf` 是都不行的,它持有的锁可能还没放开,一不留神就死锁给您看。工程上的防御,咱们手里有这么几样:映射前用 `fstat` 把大小量准。length 是自己算的就要自己兜底,比如文件头里声明的长度,完全可能是错的。和别的进程共享文件的时候,大家约好谁都不许 truncate。
:::

## mprotect:映射建好之后,改的就是权限

`prot` 可不是一锤子的买卖。映射建好了之后,咱们用 `mprotect(addr, len, prot)` 还能改,而且 man 2 mprotect 的措辞值得咱们细品:它修改的是"与 [addr, addr+size-1] 相交的所有页"的保护。效果按的是整页生效,某页只要与区间沾了边,整页的保护都会被改。`addr` 倒是必须页对齐,不然 EINVAL——取整这活儿它可不像 mmap 那样替您干,addr 您得自己对齐。POSIX 只保证对 mmap 得到的内存调它,Linux 倒是放得更宽,进程的地址空间里几乎哪儿都能改,连代码段都能给您改成可写。改完之后您越权访问,内核会送您一个 SIGSEGV,而 `si_code` 会替咱们分好病因:`SEGV_ACCERR` 说的是映射在、权限不许。`SEGV_MAPERR` 说的是地址根本不在任何映射里。两类病因的现场,咱们各看一次才作数。

现场怎么拍?每个会触发 SIGSEGV 的动作,咱们都 fork 一个子进程去干:子进程装上 SA_SIGINFO 的 handler,handler 里咱们只用 `write` 打 `si_addr` 与 `si_code`、然后 `_exit(70+场景号)`,父进程的 `waitpid` 负责收尸解读——和 SIGBUS 实验是同一副纪律。实验的对象是三页匿名 RW 映射,咱们把中间那页降成 `PROT_NONE`,这就是 **guard page** 了:栈越界探测、分配器隔离带,用的都是它。

```cpp
// e1.cpp(节选):handler 与 fork/waitpid 的骨架
volatile sig_atomic_t g_which = -1; // 场景号必须走 volatile sig_atomic_t,见下方说明
constexpr const char* kNames[] = {"", "s1 read data page", "s2 guard page read",
                                  "s3 write after RW->R", "s4 write after R->RW",
                                  "s5 nullptr read"};

void on_sigsegv(int, siginfo_t* info, void*)
{
    write_all("\n[handler] SIGSEGV in scenario \"");
    write_all(kNames[g_which]);
    write_all("\", si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    switch (info->si_code) {
    case SEGV_MAPERR: write_all(", si_code = SEGV_MAPERR(1)  <- 地址不在任何映射里\n"); break;
    case SEGV_ACCERR: write_all(", si_code = SEGV_ACCERR(2)  <- 映射在,权限不许\n"); break;
    default:          write_all(", si_code = ?\n"); break;
    }
    ::_exit(70 + g_which); // 70..74 按场景区分
}
```

`g_which` 是 `volatile sig_atomic_t`?有一处返工值得咱们原样记下来。初版咱们把场景标签放在普通 `const char*` 全局里,编译开的也是 `-O2`,跑起来 handler 打的居然是**空标签**:编译器把全局的 store 重排到了触发信号的访问之后,handler 读到的还是没初始化的值。man 7 signal 的告诫早就写在那儿了,handler 里能保证看见的只有 `volatile sig_atomic_t` 与 `volatile std::atomic`,咱们这是亲手替它验了一次货。场景标签改成编号、走 `sig_atomic_t` 传递的做法,handler 里再查只读的名字表,才算安分了。

```text
$ ./e1
anonymous 3 pages at 0x78245aadf000 .. 0x78245aae2000 (page size 4096)
mprotect([++4096, ++8192) -> PROT_NONE  (guard page)
-- scenario: s1 read data page 0            -> no signal, byte travels via exit code
   OK: child exited 81 as expected
-- scenario: s2 read PROT_NONE guard page 1  -> SIGSEGV SEGV_ACCERR

[handler] SIGSEGV in scenario "s2 guard page read", si_addr = 0x000078245aae0000, si_code = SEGV_ACCERR(2)  <- 映射在,权限不许
   OK: child exited 72 as expected
-- scenario: s3 write page 2, RW->R, rewrite -> SIGSEGV SEGV_ACCERR

[handler] SIGSEGV in scenario "s3 write after RW->R", si_addr = 0x000078245aae1000, si_code = SEGV_ACCERR(2)  <- 映射在,权限不许
   OK: child exited 73 as expected
-- scenario: s4 R->RW upgrade, write again   -> no signal, byte = 'U'
   OK: child exited 85 as expected
-- scenario: s5 dereference nullptr          -> SIGSEGV SEGV_MAPERR

[handler] SIGSEGV in scenario "s5 nullptr read", si_addr = 0x0000000000000000, si_code = SEGV_MAPERR(1)  <- 地址不在任何映射里
   OK: child exited 75 as expected
done: guard/readonly violations -> SEGV_ACCERR; unmapped -> SEGV_MAPERR
```

咱们把三行 handler 输出一行行对过去:s2 的 si_addr 是 0x78245aae0000,等于基址加 0x1000 的和,正是 guard 页的页首。s3 的是基址加 0x2000,s5 干脆落了个 0。si_code 的分工相当稳定:读 PROT_NONE 页、写只读页,来的都是 SEGV_ACCERR。而解引用空指针的场合,来的则是 SEGV_MAPERR。s1 与 s4 是没有信号的,数据怎么证明流动了?咱们让子进程把读到的字节当退出码传回来,81 对应的是 'Q',85 对应的是 'U'——退出码只有 8 位的宽度,拿它传字节咱们只图演示,您可别照搬进工程。漂移项咱们也说清楚:基址每次运行都会变的,那是 ASLR 在干的活,而"si_addr 等于出错页的页首对齐地址"的规律恒成立。

mprotect 同样管得了文件的视图,咱们还能把参数一节欠下的断言变成实测。第二个实验(e2)拿 16 字节的 AAAABBBBCCCCDDDD,O_RDWR 的 fd 配 `MAP_SHARED` 建好读写视图,另开一个 O_RDONLY 的 fd 专职 pread 旁观,然后咱们按五步走:

```cpp
// e2.cpp(节选):peek() 是 pread 16 字节的小封装,handler 与 e1 同款
std::memcpy(region.data(), "XXXX", 4);
std::print("1) write 'XXXX' as RW      : ok,  fd2 pread = {}\n", peek(fd2));

sys_call("mprotect->READ", ::mprotect, region.data(), region.size(), PROT_READ);
pid_t pid = ::fork();
if (pid == 0) {                       // 子进程在只读视图上写第一个字节
    struct sigaction sa {};
    sa.sa_sigaction = on_sigsegv;
    sa.sa_flags = SA_SIGINFO;
    ::sigaction(SIGSEGV, &sa, nullptr);
    g_which = 1;                      // 把场景号写进 volatile 变量,不许被重排
    volatile unsigned char* vp = region.data();
    *vp = 'Y';                        // SIGSEGV 应在这行爆发
    ::_exit(0);
}
int st = 0;
::waitpid(pid, &st, 0);
std::print("   parent: child exit status = {} (WIFEXITED={}, code={})\n",
           st, WIFEXITED(st), WEXITSTATUS(st));

sys_call("mprotect->RW", ::mprotect, region.data(), region.size(),
         PROT_READ | PROT_WRITE);
std::memcpy(region.data(), "ZZZZ", 4);
std::print("3) mprotect -> RW again    : ok,  write 'ZZZZ', fd2 pread = {}\n", peek(fd2));

int rc = ::mprotect(region.data() + 8, 4096, PROT_READ); // 地址不按页对齐
std::print("4) mprotect(base+8, ...)   : rc={}, errno={} ({})  <- addr 必须页对齐\n",
           rc, errno, std::strerror(errno));

void* p = ::mmap(nullptr, 16, PROT_WRITE, MAP_SHARED, fd2.get(), 0); // fd2 是只读的
std::print("5) O_RDONLY fd + PROT_WRITE + MAP_SHARED : {}",
           p == MAP_FAILED ? "MAP_FAILED" : "unexpected success");
if (p == MAP_FAILED) std::print(", errno={} ({})\n", errno, std::strerror(errno));
p = ::mmap(nullptr, 16, PROT_WRITE, MAP_PRIVATE, fd2.get(), 0); // 同一个 fd,换私有映射
std::print("   same fd, switch to MAP_PRIVATE         : {}",
           p == MAP_FAILED ? "MAP_FAILED" : "mapped (COW, legal)");
if (p == MAP_FAILED) std::print(", errno={} ({})\n", errno, std::strerror(errno));
else { std::print("\n"); ::munmap(p, 4096); }
```

```text
$ ./e2
mapped 16 bytes (rounded to 4096 B) at 0x787a3c315000, MAP_SHARED RW
1) write 'XXXX' as RW      : ok,  fd2 pread = XXXXBBBBCCCCDDDD
2) mprotect -> PROT_READ   : ok,  view is now read-only

[handler] SIGSEGV, si_addr = 0x0000787a3c315000, si_code = SEGV_ACCERR(2)
   parent: child exit status = 18176 (WIFEXITED=true, code=71)
3) mprotect -> RW again    : ok,  write 'ZZZZ', fd2 pread = ZZZZBBBBCCCCDDDD
4) mprotect(base+8, ...)   : rc=-1, errno=22 (Invalid argument)  <- addr 必须页对齐
5) O_RDONLY fd + PROT_WRITE + MAP_SHARED : MAP_FAILED, errno=13 (Permission denied)
   same fd, switch to MAP_PRIVATE         : mapped (COW, legal)
```

咱们从这一串输出里挑四处看。头一处的看点,是 handler 里的 si_addr 与第一行打印的映射基址**完全相等**,因为子进程写的是 `data()[0]`,而 fork 出来的子进程原样继承了父进程的映射,地址是不变的。第二处看的是第 4 步:errno=22 就是 EINVAL,证明 mprotect 的 addr 参数必须 `sysconf(_SC_PAGE_SIZE)` 对齐,mmap 的这套"length 替您取整"的好脾气,它可是没有的。第三处看的是第 5 步:errno=13 是 EACCES,只读 fd 配 PROT_WRITE 配 MAP_SHARED 的组合被当场拒绝,而同一个 fd 换成 MAP_PRIVATE 就合法——参数一节里纯文字的断言,到这里就是实测了。第四处请您留意打印的方式:waitpid 的原始状态字是 18176,那是 71 左移 8 位的位布局,直接端出来会吓到人的,咱们展示时一律用 `WEXITSTATUS` 解码后的 71。还有一条 man 页的告诫补在这儿:您往"以只读方式打开的文件"的映射上加 `PROT_WRITE`,mprotect 回的也是 EACCES。匿名映射没有这层文件的约束,提权到可写是没问题的。第 1 步与第 3 步的输出里,fd2 的 pread 都立刻看到了新值,这一眼的分量,咱们到 Dirty 一节再称。

## MAP_SHARED:两个进程,同一份页缓存

参数一节咱们说过,MAP_SHARED 的写"其他进程立刻可见"。这话凭什么成立?凭的是共享的方式:MAP_SHARED 的写,落的**直接就是文件的页缓存页**,内核根本不做私有的副本。而别的进程映射同一文件的同一区间,缺页时拿到的也是同一批物理页。咱们说的"立刻可见",靠的其实是大家本来就在同一块内存上写字,没有谁去通知谁的必要。咱们空口无凭,把实验摆出来:父进程建好自己的映射之后 fork,子进程**自己 open、自己 mmap** 同一文件——独立打开的文件描述,地址也和父亲的不同——写一个字节,然后经管道通知了父进程,顺带把自己映射的地址捎回来。全程是没有 msync 的。

```cpp
// e4.cpp(节选):子进程里一行 stdio 都不碰,数据全走管道
pid_t pid = ::fork();
if (pid == 0) {                                   // ---- 子进程 ----
    unique_fd own{sys_call("open", ::open, path, O_RDWR)}; // 自己的打开文件描述
    void* p = ::mmap(nullptr, 16, PROT_READ | PROT_WRITE, MAP_SHARED, own.get(), 0);
    if (p == MAP_FAILED) { ::_exit(9); }
    static_cast<unsigned char*>(p)[4] = 'X';      // 改一个字节,落在页缓存
    struct { char tag; std::uint64_t addr; } msg{'R', reinterpret_cast<std::uint64_t>(p)};
    // 结构体有对齐填充,地址字段按 offsetof 取,别猜布局
    const unsigned off = static_cast<unsigned>(__builtin_offsetof(decltype(msg), addr));
    if (::write(pfd[1], &msg, off + sizeof msg.addr) != static_cast<ssize_t>(off + sizeof msg.addr)) { ::_exit(7); }
    ::_exit(0);                                   // 退出码 0,父进程按口径收尸
}

char buf[32] {};
constexpr unsigned kAddrOff = 8; // tag 之后有 7 字节填充,addr 从第 8 字节起
const unsigned want = kAddrOff + sizeof(std::uint64_t);
for (unsigned got = 0; got < want;) {             // 管道读也要循环,01 篇的教诲
    ssize_t n = sys_call("read-pipe", ::read, pfd[0], buf + got, want - got);
    if (n == 0) { break; }
    got += static_cast<unsigned>(n);
}
std::uint64_t child_addr = 0;
std::memcpy(&child_addr, buf + kAddrOff, sizeof child_addr);
std::print("child  : its OWN mmap at 0x{:x}, wrote 'X' at [4], pinged pipe\n", child_addr);
```

```text
$ ./e4
parent : mmap MAP_SHARED 16 B at 0x710c9d355000, view = AAAABBBBCCCCDDDD
child  : its OWN mmap at 0x710c9d354000, wrote 'X' at [4], pinged pipe
parent : after pipe ping (no msync anywhere):
  own mapping [4]     = X   <- 子进程写的字节,父亲自己的映射直接可见
  pread(fd, 16 B)     = AAAAXBBBCCCCDDDD   <- 独立 syscall 路径问文件,同一个答案
  whole view          = AAAAXBBBCCCCDDDD
waitpid: WIFEXITED=true code=0 (0 = child clean)
```

父进程映射的那行地址是 0x710c9d355000,子进程的那行则是 0x710c9d354000,**不同**——子进程的映射是他自己 mmap 的,不是 fork 继承的——可父亲在自己的映射里读到的就是那个 'X'。再看 pread 的那一行,它走的是完全独立的系统调用路径,给出的答案也还是 AAAAXBBBCCCCDDDD。咱们手里有三条路:父进程的映射、pread、子进程的映射,它们在页缓存里汇成的是同一个答案。咱们要的"写落在页缓存、人人可见"的实证,这里就是了,而且输出里特意写了 no msync anywhere,可见性是不需要写回的。两个漂移项咱们交代清楚:两个映射的地址每次都变,子进程比父亲恰好低了一页,只是本机内核自顶向下分配的巧合,您别把它写成规律。子进程的退出码是 0,waitpid 的解码一切正常。咱们让子进程一行 stdio 都不碰,是有意的纪律,fork 与 stdio 缓冲的恩怨,咱们到进程篇再细算。

> 代码里还有一处值得学的小防守:捎回的地址按 `offsetof` 取。初版咱们按 tag 加 1 去解,结构体里 7 字节对齐填充让子进程的地址打成了 0x0。

## msync 与 madvise:可见是一回事,写到盘上是另一回事

MAP_SHARED 写完了之后,您改的是页缓存里的页。别的进程看得见吗?上一节刚验过,答案也是看得见的。那数据到盘上了吗?咱们不知道。"可见"其实是一件事,"已写回"其实是另一件事,咱们用 `/proc/meminfo` 的 **Dirty 计数**把它们当面分开:Dirty 记录的是全系统已修改、还没写回的页缓存页,单位记的是 kB。

实验(e5)这么设计:在真盘 ext4 上造一个 128 MiB 的文件——咱们有个纪律得讲在前面,/tmp 是 tmpfs 的地界,页全都留在了内存里,写回是永远等不来的,数据文件放那儿就白测了——第二个 O_RDONLY 的 fd 旁观。mmap 建好读写视图之后咱们每页写 1 个字节,32768 页就页页变脏了。动手写之前咱们提前 pread 过一次稀疏洞(读出来是 0),写完了立刻再 pread,然后咱们给 `msync(MS_SYNC)` 计时,最后补一段只写 1 MiB、不做 msync 的日常形态:

```cpp
// e5.cpp(节选):meminfo_kb() 扫 /proc/meminfo 取某字段的 kB 值
report("baseline");                       // Dirty 起点
for (std::size_t i = 0; i < pages; ++i) {
    region.data()[i * page] = static_cast<unsigned char>(i); // 每页摸 1 字节,页页变脏
}
report("after dirtying");
sys_call("pread-1", ::pread, fd2.get(), &seen, 1, static_cast<off_t>(page));       // 页 1
sys_call("pread-2", ::pread, fd2.get(), &seen, 1, static_cast<off_t>(12345 * page)); // 页 12345

auto t2 = std::chrono::steady_clock::now();
sys_call("msync", ::msync, region.data(), region.size(), MS_SYNC);
auto t3 = std::chrono::steady_clock::now();
std::print("msync(MS_SYNC) over 128 MiB       : {:.1f} ms\n",
           std::chrono::duration<double, std::milli>(t3 - t2).count());
report("after msync(MS_SYNC)");

for (std::size_t off = 0; off < kSmall; off += page) {   // 只写 1 MiB,不做 msync
    region.data()[off] = 0xAB;
}
report("after +1 MiB, no msync");
sys_call("pread-3", ::pread, fd2.get(), &seen, 1, 4096);
```

```text
$ sync && sleep 2 && ./e5
baseline                   Dirty =      316 kB, Writeback =      0 kB
pread byte @page 12345 before write : 0 (sparse hole reads as zero)
wrote 1 byte x 32768 pages in 32.7 ms
after dirtying             Dirty =   131388 kB, Writeback =      0 kB
pread byte @page 1 via 2nd fd now : 1 (expected 1, page index & 0xFF)
pread byte @page 12345 now        : 57 (expected 57, i.e. 12345 & 0xFF)
msync(MS_SYNC) over 128 MiB       : 50.6 ms
after msync(MS_SYNC)       Dirty =      316 kB, Writeback =      0 kB
after +1 MiB, no msync     Dirty =     1288 kB, Writeback =      0 kB
pread byte @page 1 now            : 171 (0xAB = 171, visible immediately)
```

316 涨到了 131388,Δ落在大约 131072 kB 的量级,正好凑成 128 MiB 的数,数字自己就把算术做完了。msync 之后 Dirty 落回了 316,与基线是分毫不差的。咱们再写 1 MiB,Dirty 小幅回涨到了 1288。夹在中间的两行 pread 是整场的灵魂:Dirty 还高悬着的时候,页 1 读到的是 1、页 12345 读到的是 57(12345 & 0xFF,咱们写入的字节就是页号取低八位),而动手写之前同一位置读到的是 0。页 1 读到的是新值、Dirty 却还高悬着,两件事在输出里**同时成立**了——可见与已写回,就这么被咱们当面分开了。

msync 的那 50.6 ms,才是把 128 MiB 推进 VHDX 的价,后来咱们又把这个程序原样跑了两遍,一遍跑出了 68.8 ms,另一遍跑出了 48.6 ms,它是随盘负载抖的。Dirty 的回落也值得咱们多看一眼:上面贴的输出是 316 到 316 的分毫不差,后来两遍落在了 340 与 436,比各自 280、412 的基线分别高出 60 与 24 kB。您别把这几十 kB 当成写回没做完:Dirty 本来就是系统级的计数,同机有别的进程在写脏,回落值就混进了别人的份,所以咱们看 Δ、不看绝对值。真要验证盘上的内容,咱们得 root 去 drop_caches,本实验以 MS_SYNC 的返回加 Dirty 回落为准。

> 还有一处环境因素提醒您:本机的 `vm.dirty_background_ratio=10`,15 GiB 内存的阈值约 1.5 GiB,大于咱们弄脏的 128 MiB,实验的窗口里后台写回不会抢跑。换小内存的机器重跑,Dirty 可能中途自己就往下走了,遇到了请您别慌。

`msync(addr, len, flags)` 的三个 flag,咱们现在可以对着实验讲了。**MS_SYNC** 发起的写回会一路等到完成,您想控制写到盘上的时点您就用它,上面那 50.6 ms 就是它的工作照。**MS_ASYNC** 的本意是"排队,别等",但 man 2 msync 也明说了,自 Linux 2.6.19 起 MS_ASYNC 实际上已经是 no-op:内核自己会正确地跟踪脏页、按需冲刷,"排队"的那一步无事可做。不过 POSIX 仍要求您必须给 MS_SYNC 或 MS_ASYNC 之一,可移植的代码您别省,Linux 甚至允许您两者都不写,目前的等效物就是 MS_ASYNC。**MS_INVALIDATE** 让同一文件的其他映射都失效,好让它们拿到刚写回的新值。要是指定的区间里有 mlock 住的页,它回的就是 EBUSY。

`madvise(addr, len, advice)` 是咱们给内核递话的通道:"这段映射,我打算这么用",内核会据此选预读与缓存的策略。所有常规建议都不影响程序的语义、只影响性能——**唯一的例外是 `MADV_DONTNEED`,它真的丢数据**。咱们把常用的几个过一遍:`MADV_SEQUENTIAL` 预告的是顺序访问,内核会激进预读、访问完的页尽快释放。`MADV_RANDOM` 的态度正相反,它告诉内核的就是"别替我预读",免得白搬一堆用不上的页。`MADV_WILLNEED` 说的是近期就要访问,咱们不妨提前读些页进来。

`MADV_DONTNEED` 值得咱们单独特写一段,因为三种映射的下场各不相同。共享文件映射、共享匿名映射与 shmem(比如 System V 共享内存)都算这一类的,再访问时会从底层映射文件的最新内容重新填充。私有匿名映射拿到的,是按需清零的页。您看私有文件映射,man 页的枚举里没单独点名,内核 mm/madvise.c 的注释替它表了态:"这些页脏了也可以直接扔",被扔掉的正是 COW 出来的私有副本,下次缺页会按 MAP_PRIVATE 的语义从页缓存重读,您的改动跟着副本一起消失。缺页一节 fault_cost 实验的第三趟,只读映射上 DONTNEED 之后 sum 不变、缺页应声就回来了,靠的就是"重读原文"。还有两条边界咱们得记下:用在共享映射上,DONTNEED 是不保证立刻释放物理页的,内核倒可以拖到合适的时机,但调用进程的 RSS 会立刻降下来。mlock 住的页就把路堵死了:区间里只要有它们,整段调用直接以 EINVAL 失败收了场,想只丢普通页、留下锁住的页,是办不到的。man 2 madvise 的 ERRORS 写的就是它,内核 mm/madvise.c 里看到的也是同一行为,咱们照实记下就好。内核 6.1 起倒是备了一把新钥匙:MADV_DONTNEED_LOCKED,连 mlock 住的页也一并丢,咱们本机的 `/usr/include/asm-generic/mman-common.h` 里就有它的定义,您到要用的时候再回来翻它。

madvise 递话的对象是映射后的地址区间,read 那一侧其实也有个对称的通道:`posix_fadvise(fd, offset, size, advice)`。咱们按 fd 加区间预告访问模式,`POSIX_FADV_SEQUENTIAL`、`POSIX_FADV_WILLNEED`、`POSIX_FADV_DONTNEED` 与 madvise 的同名建议一一对应,不过一个管文件与页缓存,一个管映射后的地址。`POSIX_FADV_DONTNEED` 它尝试释放的是该区间的缓存页,咱们做大文件流式处理时周期性递一句,免得把更有用的缓存挤出去。但脏页是不会被释放的,您想确保释放,fsync 就得走在它的前面。

## MAP_PRIVATE:写时复制,眼见为实

MAP_PRIVATE 的约定是"你改你的,文件不知道"。空口无凭?咱们让同一个进程开两个映射当面对质:私有映射上改 4 字节,`pread` 直接问的是文件本身,再看新开的共享映射能看到什么。最后咱们在共享映射上改 4 字节,配上一次 `msync(MS_SYNC)` 做的对照。

```cpp
// cow.cpp(节选):peek_file() 是 pread 16 字节的小封装
mapped_region priv(fd, 16, PROT_READ | PROT_WRITE, MAP_PRIVATE);
std::memcpy(priv.data(), "XXXX", 4); // 写的是 COW 出来的私有副本
std::print("file via pread   : {}\n", peek_file(fd)); // 文件纹丝不动

mapped_region shared(fd, 16, PROT_READ | PROT_WRITE, MAP_SHARED);
std::print("MAP_SHARED view  : {:.16s} (sees the ORIGINAL)\n",
           reinterpret_cast<const char*>(shared.data())); // reinterpret 转成 const char*,截断交给格式串

std::memcpy(shared.data() + 8, "YYYY", 4); // SHARED:直接改页缓存
sys_call("msync", ::msync, shared.data(), shared.size(), MS_SYNC); // 写回并等待
std::print("after msync file : {}\n", peek_file(fd));
```

```text
$ ./cow
MAP_PRIVATE view : XXXX... (just wrote)
file via pread   : AAAABBBBCCCCDDDD
MAP_SHARED view  : AAAABBBBCCCCDDDD (sees the ORIGINAL)
after msync file : AAAABBBBYYYYDDDD
```

结果就摆在这儿,您看:私有映射里明明写着 XXXX,`pread` 问回来的还是 AAAA,新开的共享映射看到的也是原件。您的修改只活在私有副本里,是进不了文件的,别的进程谁也看不见。共享映射那边写完了再加 `msync(MS_SYNC)`,YYYY 立刻就出现在文件里了。结合上一节的 Dirty,咱们现在能把 MAP_PRIVATE 与 MAP_SHARED 的差别说到字节级:前者写的是 COW 副本,连 Dirty 都是不惊动的。后者写的就是页缓存页,Dirty 也会应声上涨的,写回只是时间的问题。

## 512 MiB 顺序读:两台机器,两个答案

开篇欠下的那场对比,现在咱们来兑现:512 MiB 的文件用 dd 生成,read 用一口 1 MiB 的缓冲循环读、mmap 整段求和,两边算的是同一个校验和。文件咱们放在 /tmp,两边挂的都是 tmpfs、常驻页缓存,磁盘这个变量咱们把它排除在外,量到的是纯 CPU 侧的成本。下面的节选就是这场对比的骨架,台机那一轮跑的是它的前身,笔记本那一轮的 bench,咱们就是照它复原的。

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

台机(Ryzen 7 9700X)的数字咱们原样保留,那是前版的实测,当时笔者没把代码存下来:

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

笔记本(i7-13700H)这一轮的 bench 是咱们照节选复原的,代码与全部输出收进了仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/02-mmap-memory-mapping/09-bench/`:

```text
$ for i in 1 2 3; do ./bench read big.bin; ./bench mmap big.bin; done
read     :   110.4 ms, 4636.9 MiB/s, sum 68450753178, minor faults 446
mmap     :    79.1 ms, 6469.4 MiB/s, sum 68450753178, minor faults 8381
read     :    97.9 ms, 5230.3 MiB/s, sum 68450753178, minor faults 447
mmap     :    75.1 ms, 6819.7 MiB/s, sum 68450753178, minor faults 8380
read     :   107.0 ms, 4783.1 MiB/s, sum 68450753178, minor faults 447
mmap     :    76.7 ms, 6679.2 MiB/s, sum 68450753178, minor faults 8380
$ ./bench populate big.bin
mmap(MAP_POPULATE) itself took 8.8 ms
populate :    73.3 ms, 6988.0 MiB/s, sum 68450753178, minor faults 8514
```

两台机器的输出摆在一起,咱们一台一台看稳态。第一轮咱们当热身(频率爬坡、缓存预热),只看后两轮的稳态:台机上 read 稳定在 76~77 ms,mmap 守着 89~90 ms 的成绩,**顺序读 read 赢了约 15%**。咱们再看笔记本,恰好反了过来:read 落在 98~107 ms,mmap 只用了 75~77 ms,**mmap 赢了约四分之一**。同一段代码跑出了两个方向,胜负跟着机器走了。

fault 计数给咱们留的是一条可观测的线索。两机的格局倒在同一量级:read 一轮只有四百多次 minor fault(程序与缓冲本身的那点),mmap 一轮吃到了八千多次,fault-around 批发出的就是约 8000 次。真差出来的是单价:回到缺页一节 fault_cost 的数字,同样一千次上下的缺页,台机首摸付了 7.20 ms,笔记本只付了 1.42 ms,差了约五倍。同样多的缺页,在台机把省下的那次 512 MiB 拷贝压过去了,换到笔记本就压不过了。胜负为什么会翻转,可观测的解释到这儿就到头了,硬件成因咱们不深究。MAP_POPULATE 两机也都没翻出花样:台机的 37 ms 挪进了 mmap 调用,总时间落在了 92.5 ms,还略亏了一点。笔记本的 8.8 ms 挪了进去,总时间落在了 73.3 ms,与不 populate 的成绩基本持平。您也别拿系统调用说事:512 次 read,按[总纲](../../00-overview.md)实测的约 125 ns/次,总共也就 64 µs 的量,根本就不是什么瓶颈。

那 mmap 什么时候赢?随机访问大文件的时候,只碰您要的页,read 要么预读浪费、要么 syscall 太碎。多进程共享同一份文件的映射,也是 mmap 的主场,动态链接器加载 so 就是教科书级的案例,一份物理页换来全员的共享。还有把文件当内存里的结构直接用,或者写路径想要零拷贝的场合。什么时候轮到用 read?小一点的文件,或者您要跨平台一致的行为,再或者干脆不想伺候 SIGBUS 的场合。至于纯顺序的大扫描,预读本来就是替 read 优化的,可咱们今天也亲眼看见了,预读的优势在台机上守住了,到了笔记本就没守住。您自己手里的机器值哪个答案,拿上面的 bench 量一遍,几分钟的事。

## 另一侧怎么看

Windows 是没有 mmap 的,同一件事它分成了两步走:`CreateFileMapping` 造出一个映射对象,把文件的 HANDLE 与页保护都登记好,再由 `MapViewOfFile` 把它贴进本进程的地址空间,后者才是 mmap 的对应物。MAP_PRIVATE 的镜像叫 FILE_MAP_COPY,同样走的是写时复制。`msync(MS_SYNC)` 咱们得靠 `FlushViewOfFile` 加 `FlushFileBuffers` 组合出来,单用前者只发起脏页的写回,并不等写到盘上——它停在 MS_ASYNC 与 MS_SYNC 的中间。最要当心的差异是:Linux 的 SIGBUS 剧本,也就是文件被截短了再摸越界区,在 Windows 是根本拍不成的,截短的那一步就被 `SetEndOfFile` 拦下了,ERROR_USER_MAPPED_FILE 把事故拦在了发生以前。写只读视图送来的是 ACCESS_VIOLATION 结构化异常,映射视图底下的 I/O 要是出了错,送来的则是 EXCEPTION_IN_PAGE_ERROR。展开的实现,咱们见镜像篇:[文件映射:CreateFileMapping 与 MapViewOfFile](../../windows/file-io/02-file-mapping.md),欢迎您过去对读。

## 小结

这一篇咱们实测确认下来的东西,都收在这儿了,方便您回头查:

- mmap 六参数:`addr` 传 nullptr 最可移植,非空只是建议,`MAP_FIXED` 会静默覆盖既有映射,`MAP_FIXED_NOREPLACE`(4.17 起)撞车报 EEXIST 才是安全姿势。`length` 内核按页取整、`prot` 别和 open 冲突(`MAP_SHARED`+`PROT_WRITE` 要求 fd 可写)、`MAP_SHARED`/`MAP_PRIVATE` 定语义、`offset` 必须 `sysconf(_SC_PAGE_SIZE)` 的整数倍、`fd` 映射后即可关
- 失败返回 `MAP_FAILED` 不是 nullptr。`mapped_region` 用 RAII 管起来,move-only,取整后的真实长度,咱们认 `size()` 报的这份就行
- 映射建立零成本,首次触碰才缺页:台机实测首摸约 0.44 µs/页,二次访问约 9 ns/页(笔记本上 0.086 µs 与 15 ns,首摸更贵这件事跨机型成立)。fault-around 让 16384 页只花约 1028 次缺页。minor 与 major 的分界是要不要动磁盘,计数在 /proc/self/stat 第 10 字段
- /proc/self/maps 六列:一个映射一行,s/p 对应 SHARED/PRIVATE,设备号与 inode 能对到具体文件,mmap/munmap 就是加一行减一行
- 文件被 truncate 后摸越界区收到 SIGBUS 信号而非错误码。handler 只能异步信号安全。防御靠 fstat 和长度兜底
- mprotect 建好之后改权限,addr 必须页对齐,效果按整页生效。越权访问的 si_code 分工:SEGV_ACCERR 映射在权限不许,SEGV_MAPERR 地址没映射(实测)。guard page 就是 PROT_NONE 的一页
- MAP_SHARED 的写直接落在页缓存,跨进程立刻可见,不需要 msync(实测三条路径同一答案)。可见不等于已写回:Dirty 计数把这两件事当面分开了,msync(MS_SYNC) 50 ms 级,MS_ASYNC 自 2.6.19 起是 no-op
- madvise 递访问模式,常规建议只影响性能,MADV_DONTNEED 例外:共享映射从文件重填,私有匿名映射清零,私有文件映射丢的是 COW 副本。read 侧对称的通道是 posix_fadvise
- MAP_PRIVATE 写时复制不写进文件(实测 XXXX 永远进不了文件)。512 MiB 顺序读两台机器两个答案:台机 read 快约 15%,笔记本 mmap 快约四分之一,咱们观测到的解释是缺页单价差约五倍。mmap 稳赢的场合,咱们记随机访问、共享与零拷贝

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="2"
    title="mprotect(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mprotect.2.html"
  />
  <ReferenceItem
    :id="3"
    title="msync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/msync.2.html"
  />
  <ReferenceItem
    :id="4"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="5"
    title="posix_fadvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/posix_fadvise.2.html"
  />
  <ReferenceItem
    :id="6"
    title="proc_pid_maps(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html"
  />
  <ReferenceItem
    :id="7"
    title="proc_pid_stat(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html"
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
