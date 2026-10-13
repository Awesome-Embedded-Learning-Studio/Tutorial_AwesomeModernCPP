---
title: "错误处理范式:从 errno 到 expected"
description: 系统调用失败只交回一个 -1,细节留在 errno 与 GetLastError 一对线程局部的槽位里;本篇实测槽位的读取窗口(Linux 侧 11 种插入 3 项污染、Windows 侧 29 个成功调用逐一过哨兵,抓出清零、留杂音、保留三种行为),定义全系列的 errno_code/sys_call/last_error_code,失败分支头一行把槽位值定格进 error_code;EINTR 在 sys_call 内部重试,strace 证据链对照 SA_RESTART 的内核重启;再统一到工具层 expected、应用顶层 system_error 的双出口,汇编旁证给出零开销的成立边界
chapter: 8
order: 2
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 28
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "error_code：错误码体系与自定义 category"
  - "expected：值或错误，C++23 的错误处理新范式"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - expected
  - 工程实践
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 错误处理范式:从 errno 到 expected

前一篇《[OS 资源的 RAII 范式](01-raii-paradigm.md)》里,咱们用同一副 move-only 的 RAII 骨架管住了 fd、句柄、内存映射这些 OS 资源,释放写进了析构函数,资源从此就不漏了。可是失败本身怎么报告,咱们还一个字没写。您随手调一次 `::open()`,它要是失败了,交回来的就只有一个 -1:谁失败了、为什么失败,返回值里是一概查不到的。细节留在哪儿?Linux 把它放进 errno,Windows 把它放进 GetLastError 的返回值里,而两侧的住处有个共同点:都是每个线程一份的槽位,而且只在失败发生后的那一小会儿里保真。

本篇只办这个槽位的事。它住在哪儿、谁改得动它、什么时候读它才算数、读出来怎么变成 C++ 里能安全传递的值,咱们就按这个顺序往下走。最后落成三件全系列沿用的公共工具:Linux 侧的 `errno_code` 与 `sys_call`,Windows 侧的 `last_error_code`,再配上一条双出口的约定:工具层传错误用的是 expected,应用顶层的做法是把错误升级成 system_error。两侧的实验都是实测。Linux 这边跑的是 WSL2:内核 6.18.33.2、g++ 16.2.1、glibc 2.44、strace 7.2。Windows 这边跑的是 Win11 26200,编译器是 MSYS2 UCRT64 的 g++ 16.1.0。全部的代码与原始输出,收在 `code/volumn_codes/vol8/systems-programming/` 两侧的 `thinking/02-error-paradigm/` 目录里,复现命令也写在各自的 README 里,您随时可以对表。实验编号咱们也交代在开头:Linux 侧的实验按 E1 到 E4 编号,编号跟着存档的目录走,出场的顺序是 E1、E4、E2、E3。Windows 侧的实验号是小写的 e,接着前一篇的 e1、e1b 往后排,和 Linux 的 E 系列互不相干,后文的 Windows e4 与 Linux E4 是两个实验。两侧的成对篇章同样有短名:Linux 侧的《POSIX 文件 I/O》简称 L01,Windows 侧的《Win32 文件 I/O》简称 W01,镜像篇咱们都用这样的叫法。

## errno 住哪儿:每个线程一份的槽位

咱们从 Linux 侧开场。man 3 errno 的 DESCRIPTION 里有一句直接的话:errno 是线程局部的,一个线程设置它、也不会影响别的线程里的值。落到 glibc 的实现上,errno 这个宏最终会展开成对一个函数返回值的解引用,咱们每回读写 errno,都经由 `__errno_location()` 拿到属于本线程的 int。所以多个线程各自失败,谁也不会把谁的错误码写花。

同一段原文里还有更要紧的一句:errno 的值,只在调用的返回值表明出错的场合才有意义。成败的判断,咱们永远看返回值,errno 则是失败之后才去查的细节。这句话反过来读也有味道:一次成功的调用之后,errno 里躺着的可能是更早某次失败留下的旧码,您要是拿 errno 判断成败,读到的数字跟这次调用根本对不上号。

既然咱们只在失败后读,就给读取定下一个固定的形状。下面是全系列 Linux 侧的公共工具之一,本篇是它的定义处,后面的篇章一律引用、不再重定义:

```cpp
// 公共工具(系列沿用):失败后立刻调用,errno 的值在这一刻定格进 error_code
std::error_code errno_code() noexcept
{
    return std::error_code{errno, std::generic_category()};
}
```

定义完咱们当场就用,这就是 E1:errno 装箱与线程局部性的实验,下面的三段输出都出自它。open 一个不存在的路径,再对已关闭的 fd 做一次 write,两次失败的头一行都调 `errno_code()` 完成装箱。把转瞬就可能被改写的槽位值,拷进了 error_code 这个普通值对象,咱们在这个系列里就叫它装箱。选 `generic_category` 也有它的道理:POSIX errno 的编号恰好就是 `std::errc` 枚举的值域来源,所以挂了它的 error_code,`message()` 吐的就是 `strerror()` 那句话,还能直接跟 `std::errc` 的枚举值比较:

```cpp
int fd = ::open("/tmp/errpar/no_such_file_e1", O_RDONLY);
if (fd == -1) {
    std::error_code ec = errno_code();   // 失败分支的头一行
    std::printf("    ec.value() = %d   (ENOENT = %d)   equal = %s\n",
                ec.value(), ENOENT, ec.value() == ENOENT ? "yes" : "NO");
    std::printf("    ec == std::errc::no_such_file_or_directory : %s\n",
                ec == std::errc::no_such_file_or_directory ? "true" : "false");
}
```

```text
[1] open(no_such_file_e1) failed:
    ec.value() = 2   (ENOENT = 2)   equal = yes
    ec.message() = "No such file or directory"
    ec == std::errc::no_such_file_or_directory : true
[2] write to closed fd: return = -1 (expect -1)
    ec2.value() = 9   (EBADF = 9)   equal = yes
    ec2.message() = "Bad file descriptor"
```

您看,值是对得上的,文本是可读的,errc 的判等也通,这正是后面所有错误处理要的地基。线程局部的那一句,咱们再用 E1 的双线程交错验一遍:线程 A open 一个不存在的路径,造出了 ENOENT、置起一个 atomic 标志。线程 B 等到 A 的标志、造出自己的 EBADF、也置上标志。两边都等对方失败完了才各自调 `errno_code()` 取值。要是 errno 是一个普通的全局变量,A 取值时看到的必然是 B 的 9:

```cpp
std::atomic<bool> a_failed{false};
std::atomic<bool> b_failed{false};

std::thread ta([&] {
    int f = ::open("/tmp/errpar/no_such_file_A", O_RDONLY);  // 本线程 errno = ENOENT
    (void)f;
    a_failed.store(true, std::memory_order_release);
    while (!b_failed.load(std::memory_order_acquire)) { }   // 等 B 也失败完
    std::error_code ec = errno_code();                      // 交错之后才取值
    /* 打印 ec.value() */
});
std::thread tb([&] {
    while (!a_failed.load(std::memory_order_acquire)) { }   // 等 A 失败
    int f = ::open("/dev/null", O_WRONLY);
    ::close(f);
    ssize_t r = ::write(f, "x", 1);                         // 本线程 errno = EBADF
    (void)r;
    b_failed.store(true, std::memory_order_release);
    std::error_code ec = errno_code();
    /* 打印 ec.value() */
});
```

```text
[3] two threads, interleaved failures:
    [thread A] value = 2  (expect ENOENT=2) -> kept own value
    [thread B] value = 9  (expect EBADF=9) -> kept own value
```

咱们复跑了三轮、一次也没串。线程局部防住了跨线程串味,可它防不住同一个线程里的下一个调用,咱们接下来要看的读取窗口,说的就是这件事。

## 读取窗口:失败之后到读码之前,中间隔的每一步都可能改写它

教科书上常见的一句话说,失败的调用和读取 errno 之间,别夹别的调用。咱们不背句子、直接排实验(E4)。每个场景三步:造一次基线的失败、插入一个操作、立刻读 errno 比对,值变了就算污染。基线做了两条、ENOENT=2 和 EBADF=9 各跑一遍,原因您一想就明白:插入的操作要是把 errno 改成了恰好等于基线的值,单一基线就测不出来了。存档输出里有四个探针(0/1/3/4)前面还夹着一行 stderr 的插入提示,咱们略去、只留判定行:

```text
$ ./e4 enoent
baseline mode = enoent; probes = 11
[ 0] fprintf(stderr, ...)                 baseline= 2 after= 2  unchanged
[ 1] successful write(2, ...)             baseline= 2 after= 2  unchanged
[ 2] strerror(ENOMEM)                     baseline= 2 after= 2  unchanged
[ 3] printf to stdout (buffered)          baseline= 2 after= 2  unchanged
[ 4] std::cout << (iostreams)             baseline= 2 after= 2  unchanged
[ 5] malloc 1 MiB + free                  baseline= 2 after= 2  unchanged
[ 6] std::string 4 KiB (heap)             baseline= 2 after= 2  unchanged
[ 7] fopen("/tmp") + fclose               baseline= 2 after= 2  unchanged
[ 8] getaddrinfo("localhost") ok          baseline= 2 after= 6  POLLUTED
[ 9] SECOND FAILING open()                baseline= 2 after= 2  unchanged
[10] fprintf(stderr) with fd 2 CLOSED     baseline= 2 after= 9  POLLUTED
summary: 2 of 11 probes polluted errno in this run
```

0 到 7 号的那批插入,stderr 打印、裸 write、strerror、缓冲 printf、iostream、malloc、堆上的 string、fopen,全是咱们失败处理路径上最容易夹带的活儿,glibc 2.44 的实测里八项全都没动 errno。换 EBADF 的基线再跑一遍,第 9 项就现形了:9 变成了 2、判作 POLLUTED,又一次失败的调用当然会覆盖槽位,只是它在 ENOENT 基线下伪装成了没变。真正意外的是第 8 项,咱们把它单独收窄了一把:

```text
numeric AI_NUMERICHOST       rc= 0 errno after=0
plain numeric (no flag)      rc= 0 errno after=0
name "localhost"             rc= 0 errno after=6
name again (cache warm)      rc= 0 errno after=6
```

`getaddrinfo` 成功返回了,errno 里躺着的却是 6(ENXIO)。走数字地址的路径,errno 的值保持 0。走名字解析的路径,污染是确定性复现的。咱们复跑时用 strace 跟过一遍,trace 里找不到一次失败的 syscall,所以这个 6 没经过内核,它来自名字解析路径上的用户态代码,是 glibc 的 NSS 模块直接赋的值。NSS 的全称是 Name Service Switch、名字服务开关,glibc 靠它按 `/etc/nsswitch.conf` 的排布决定主机名找谁问。具体哪个模块赋的值,要按您机器的 nsswitch 配置查,咱们不点名。网上有种流传的说法,把这归因于 `/etc/gai.conf` 的缺失或者 nscd 没有安装,笔者的机器上 gai.conf 在,systemd-resolved 也应答成功了,污染照样出现了。所以咱们只认实测的口径:不同配置下的名单不同,这是机器相关的。

man 3 errno 的 NOTES 对此早有交代,原文说了两件事:成功的函数也允许改写 errno。而任何系统调用和库函数都不会把 errno 置成零。后半句值得您多看一眼,Linux 这边压根没有成功清零的说法,读到旧码是它的常态症状。落到咱们手上的守则也只有一条:别赌成功的库调用不动 errno,POSIX 的规范里没有这个承诺,把读取固定在失败分支的头一行,就是 `errno_code()` 注释里写的那句话。

## Windows 侧的镜像:GetLastError 的三种形态

咱们把镜头切到 Windows。失败值按 API 各戴各的,哨兵的取值有三种:失效句柄的 INVALID_HANDLE_VALUE,空指针的 NULL,真假的 FALSE。三档判法的全表,咱们到 Windows 侧文件 I/O 那一篇(开头约定过的 W01)再列,您到那边对表。细节同样留在每线程的槽位里,读它的函数是 GetLastError,官方文档的原话同样直白:错误码按线程维护,一个线程不会覆盖另一个线程的值。Return value 一段还承认得很坦率:多数函数到了失败时才设错误码,但是有些函数成功时也设。文档没写会设错误码的函数,返回的只是最近一次被设下的值,而有些函数成功时把槽位设成 0,也有不设的。

文档既然自己都说得这么活,咱们就把常见的 Win32 调用挨个请进来跑一遍,一组固定的候选探针连续测试,测试的行当里管它叫 test battery、直译是电池,咱们后文就借这个叫法。咱们用哨兵法:`SetLastError(2)` 假装一次 CreateFileW 的失败刚留下 ERROR_FILE_NOT_FOUND,只调一个目标的 API、再读槽位。报 2 的,说明的是它没动。报 0 的,说明成功路径清了槽。报出别的值的,说明成功路径留下了自己的杂音。电池一共排了 30 行、29 个成功调用,外加一行故意撞独占锁失败的 CreateFileW,当成行内的对照混排在里面。对照组里再补两个失败的调用(这些实测出自 Win11 26200,系统升级后的行为可能漂移、复跑以电池输出为准):

```text
== 成功的调用对 last-error 槽位的影响(哨兵=2)==
  GetProcessId(GetCurrentProcess)    -> err=2    保留
  CreateFileW(真成功,开空闲文件) -> err=0    清零!
  CreateFileW(撞独占锁,失败32) -> err=32   改成杂音!
  ReadFile(成功)                   -> err=2    保留
  GetComputerNameW(成功)           -> err=203  改成杂音!
  ... (其余 25 行:WriteFile、FindFirstFileW、RegOpenKeyExW、Sleep(0)、
       new/delete、fprintf(stderr) 等,全部保留)

== 失败的调用一定覆盖槽位(对照组)==
  GetProcessId(野句柄,失败)      -> err=6
  CreateFileW(又一个不存在,失败)  -> err=2
```

三种形态全抓到了。清零形态最典型的样本就是 CreateFileW 本尊:您前一脚开文件失败,后一脚再开一个别的文件成功,槽位就归了零,错误码没了。混排的“撞独占锁,失败32”咱们说明一下,它是故意放进成功段里的失败调用,32 来自这一次的真失败,它不走报别的等于成功路径留杂音的那套判读。GetComputerNameW 留下的 203 是 ERROR_ENVVAR_NOT_FOUND,说的是系统没找到要找的环境变量,大概率是它内部探测环境变量时留下的一次失败,其原因文档里是没写的,咱们不猜、只记现象。对照组则明示了:失败的调用一定覆盖槽位,连值带语义都是要换的。

咱们把两侧摆到一起看,对称的和不对称的都有。对称的是官方口径:man 3 errno 说成功的函数允许改写,Microsoft Learn 的文档说有些成功时设 0、有些不设,两边都没有成功的调用不动错误码的承诺。不对称的是症状:Linux 从不清零,您读到的是旧码。Windows 是有清零型的,您读到的是 0,乍看还以为是没有出错的。两种读法给出的,都不是本次失败的真相。落到咱们的手上,答案两侧同款:失败分支的头一行,咱们立刻取值。

## 装箱即定格:last_error_code 与两侧的 category

槽位里读出来的只是个 int,要让它的值能安全传递、能比较、能产文本,咱们把它装进 `std::error_code`。error_code 的内部就两样东西:一个 int 值,加一个指向 error_category 的指针。值是拷贝进去的,装完了之后,槽位再怎么折腾都动不了它,这就是咱们说的装箱即定格。Windows 侧的装箱函数,全系列也只定义这么一次:

```cpp
// GetLastError 立即装箱:Win32 错误码挂 system_category
inline std::error_code last_error_code() noexcept
{
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}
```

定义完了,咱们当场验定格。CreateFileW 开一个不存在的文件,失败分支的头一行装箱。随后咱们故意再开一个必然成功的文件,也就是电池里的那个清零型操作,回头再看([1] 的后三行是 message 的两套渲染,咱们到编码一节再细看,此处就不贴了。标题行括号里的 e2 是采证时的叫法,清零行为的实测出自 e2b 的电池):

```text
[1] CreateFileW(不存在的文件)失败,立刻装箱
    返回值是不是 INVALID_HANDLE_VALUE:是
    ec.value()=2  ec.category().name()=system
[2] 装箱后,故意插一次会清零槽位的成功 CreateFileW(e2 实测它成功时置 0)
    此时 GetLastError()=0(成功调用已把槽位清零)
    但 ec.value() 仍是 2 —— 装箱即冻结,这就是"立即"的意义
```

槽位已经归零了,咱们手里的 `ec.value()` 还是 2。咱们前面担心的那些凶险,到这一行就结束了:只要装箱发生在失败分支的头一行,后面发生什么都不影响手里的值。

category 的这一层,MinGW 的 libstdc++ 还给咱们备了一份惊喜。`system_category().default_error_condition()` 内置了 Win32 错误码到 errno 的映射:

```text
  system(2) -> condition{value=2, category=generic, message="No such file or directory"}
  system(13) -> condition{value=22, category=generic, message="Invalid argument"}
  system(32) -> condition{value=16, category=generic, message="Resource device"}
  system(87) -> condition{value=22, category=generic, message="Invalid argument"}
```

判等的环节咱们直接受益:error_code 跟 errc(本质是 error_condition)比较时,走的正是 default_error_condition 的映射,所以 `ec == std::errc::no_such_file_or_directory` 挂在 Win32 错误码上实测为 1,一套判等的代码、两侧通吃:

```text
    ec == error_code(2, system_category) : 1  (同 category 同值)
    ec == error_code(2, win32_category)  : 0  (值同,category 不同)
    ec == errc::no_such_file_or_directory : 1  (system_category 内置映射桥接)
```

这里咱们得打起精神,等会儿的判等就指着它:同一个数字,在两套编号体系里指的不是同一个错误。Win32 的 13 是 ERROR_INVALID_DATA,说的是数据无效。errno 的 13 才是 EACCES,说的才是权限不足。咱们实测 `error_code(13, system_category()) == errc::permission_denied` 为 0,映射后的 22(EINVAL)才是它在 errno 里的对应物。所以判等别拿裸数字硬对,要么就在同一个 category 的内部比。同值不同 category 的两个 error_code 永远不相等,上面第二行的 0 就是证明。

`message()` 的编码是另一桩要交代的事。MinGW 的 libstdc++ 在 Windows 上,`system_category().message()` 吐的确实是 Win32 文本,但字节是 ANSI 代码页的,咱们在笔者的中文系统上看到的就是 GBK:

```text
  system(2) 原始字节 -> "ϵͳ�Ҳ���ָ�����ļ���"   hex: cf b5 cd b3 d5 d2 b2 bb b5 bd d6 b8 b6 a8 b5 c4 ce c4 bc fe a1 a3
  system(2) GBK转UTF8 -> "系统找不到指定的文件。"
```

> 机制佐证咱们也留了。拿 objdump -p 查 libstdc++-6.dll 的导入表,咱们在里面也见到了老熟人 FormatMessageA,旁边跟着的还有 GetLastError。libstdc++ 用的正是 A 版 FormatMessage,而 A 版天然按 ANSI 代码页产文本。

想要可靠的可读文本,咱们走 FormatMessageW 配 `WideCharToMultiByte(CP_UTF8)` 的正路,W01 的 `win32_text` 就是这么写的。要是想让 `message()` 的输出本身直接可读,咱们自己补一个 category,message 走 W 版、判等的桥接委托给 system_category:

```cpp
class win32_category_t : public std::error_category
{
public:
    const char* name() const noexcept override { return "win32"; }
    std::string message(int ev) const override
    {
        wchar_t* buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, (DWORD)ev, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        std::wstring ws = buf ? buf : L"(unknown win32 error)";
        LocalFree(buf);
        // ...去掉尾部 \r\n,再做与 win32_text 相同的两步转换,产出 UTF-8
        return to_utf8(ws);
    }
    // 判等桥接:委托给 system_category,沿用它内置的 win32->errno 映射
    std::error_condition default_error_condition(int ev) const noexcept override
    {
        return std::system_category().default_error_condition(ev);
    }
};
inline const win32_category_t& win32_category()
{
    static win32_category_t c;
    return c;
}
```

这么一来,`message()` 咱们直读就是 UTF-8,判等的能力也没丢,`error_code(2, win32_category()) == errc::no_such_file_or_directory` 咱们实测同样得 1。到了这儿,两侧的错误都成了同一种东西、一个 error_code。再往上的路,两侧就并成一条了。

## sys_call:判错、装箱、EINTR 重试,一起收走

错误报告的散装写法,您大概也写过:每调一次的 syscall、后面跟一句 `if (r == -1)`。咱们把判错和装箱收进一个模板,全系列 Linux 侧的第二件公共工具:

```cpp
int g_eintr_retries = 0;  // 实验计数器:sys_call 在内部消化掉的重试次数

// 公共工具(系列沿用):任何"返回 -1 表失败"的 syscall 都从这儿过。
// EINTR 不算错,从头再来;其余 errno 装箱抛 system_error
template <class F, class... Args>
auto sys_call(const char* what, F&& f, Args&&... args)
{
    for (;;) {
        auto r = std::forward<F>(f)(std::forward<Args>(args)...);
        if (r == -1) {
            if (errno == EINTR) {
                ++g_eintr_retries;
                continue;
            }
            throw std::system_error{errno, std::generic_category(), what};
        }
        return r;  // 0(EOF)不是 -1,原样放行
    }
}
```

咱们用起来就是 `sys_call("read", ::read, fd, buf, n)`。失败抛的就是 `std::system_error`,errno 装进的是 generic_category,`what` 成为异常消息的前缀,咱们 catch 到手,一眼能看出是哪一步炸的。EOF 返回的是 0、不是 -1,`sys_call` 对它是原样放行的,判断的事留给调用方,这是刻意的设计。模板里新出的东西只有一个:那个 `for` 循环,它专门伺候的就是 EINTR。

EINTR 是什么?您在阻塞的 read 上等数据,一个信号到了,内核把您的进程叫醒去跑信号处理函数,read 没法继续等了、只好交回 -1,errno 就置成了 EINTR,意思是这次等待被信号打断了。它不是设备出的错,也不是数据没了,处理的方式就一句话:从头再调一次。

光说是没有用的,咱们排一场能观察的实验(E2)。父进程在管道的读端阻塞 read,慢速的 fd、没数据就永远等。子进程睡满了 200 毫秒、用 `kill(SIGUSR1)` 打断父进程,再睡 200 毫秒的功夫,往管道里写下的就是 `"ping\n"`。挂信号处理函数用的是 sigaction,POSIX 登记信号处置的接口,处理函数怎么跑、被它打断的系统调用要不要重启,都由它的 sa_flags 说话。两种模式唯一的差别,就是咱们动没动 SA_RESTART 的设置:

```cpp
struct sigaction sa{};
sa.sa_handler = on_signal;
sigemptyset(&sa.sa_mask);
if (restart)
    sa.sa_flags = SA_RESTART;   // 两种模式只差这一行
```

咱们把两种模式各跑一遍,信号都送达了,最终都读到了 5 个字节,唯一的差别落在计数器上(输出里引号中的换行是真实的,`"ping\n"` 的 `\n` 一起被打印,收尾的引号才另起一行。每组末尾还有第二次读的 EOF 透传和异常版的错误路径两行,咱们略去):

```text
$ ./e2 norestart
mode = norestart (SA_RESTART OFF: read returns -1/EINTR)
read returned 5 bytes: "ping
"
signal delivered = 1, EINTR retries inside sys_call = 1

$ ./e2 restart
mode = restart (SA_RESTART ON : kernel restarts read for us)
read returned 5 bytes: "ping
"
signal delivered = 1, EINTR retries inside sys_call = 0
```

不设 SA_RESTART 的那一档,sys_call 在用户态重试了一次。带上了它,重试的次数是 0。中间到底发生了什么,strace 替咱们记了全程。咱们看不带 SA_RESTART 那次运行从发信号到拿到数据的完整时序(父子两个 pid,加载器的杂音行已略):

```text
kill(106137, SIGUSR1 ...)                # 子进程发信号
<... read resumed>, 0x7ffc..., 64) = ? ERESTARTSYS (To be restarted if SA_RESTART is set)
--- SIGUSR1 {si_signo=SIGUSR1, si_code=SI_USER, si_pid=106181, ...} ---
rt_sigreturn({mask=[]})                  = -1 EINTR (Interrupted system call)
read(3 <unfinished ...>                  # sys_call 的重试入口
<... read resumed>, "ping\n", 64) = 5
```

时序咱们按内容过。头一行的 kill,是子进程把 SIGUSR1 发了出去。跟着的 read resumed,是被阻塞的 read 在内核里被打断,strace 显示的 ERESTARTSYS 是内核内部的返回码,内核自己的 errno.h 里给这组值的注释写得很硬:它们 “should never be seen by user programs”,ptrace 能在 syscall 退出的跟踪点观察到,但绝不会留给被调试的用户进程。所以它永远不会逃到用户态,咱们能见着它,靠的正是隔着 ptrace 的旁观通道,内核注释与咱们的观察位置互扣。它旁边的注释也把语义写明白了:要是设了 SA_RESTART,内核会重启这次的调用。中间的 SIGUSR1 行,是信号送达、处理函数开始跑的时刻。rt_sigreturn 是处理函数返回时走的系统调用,负责恢复被打断的现场,它右侧的 -1 EINTR,就是内核写进保存现场、随恢复落到用户态寄存器上的 read 返回值,glibc 的 read 包装看到它,置好了 errno、交回 -1。末尾的两行,新的 read 入口是 `sys_call` 里的 `continue` 发出的,数据到位了,5 个字节到了手,程序自己数的 retries=1 与它互证,证据链是闭合的。

带上 SA_RESTART 的对照 trace,咱们看,形状跟前一份几乎是分不出来的,差别只有两处(块内的 ... 省略的是与前一份相同的打断时序:kill、read 的 ERESTARTSYS、SIGUSR1 送达,不同的只有 pid 和地址这类每次运行必变的字段):

```text
rt_sigaction(SIGUSR1, {..., sa_flags=SA_RESTORER|SA_RESTART, ...}) = 0
...
rt_sigreturn({mask=[]})                  = 0
read(3 <unfinished ...>                  # 内核的 rewind 重执行,不是用户态新调用
<... read resumed>, "ping\n", 64) = 5
```

咱们看 rt_sigreturn 右侧,它显示的是 0,随后的 read 入口,来路也变了。带 SA_RESTART 的重启,做在恢复现场的内容上:内核在信号路径里把保存的现场改写成还没进过 read 的样子,指令的指针退回 syscall 指令本身,寄存器 rax 里放回原始的系统调用号。rt_sigreturn 一返回、CPU 把用户态的 syscall 指令重新执行一遍、再次陷入内核,strace 于是又看见了一次 read 入口。x86-64 上 read 的调用号 `__NR_read` 恰好是 0,这正是 rt_sigreturn 右侧那个 0 的来历:read 本身还没返回,那个 0 是内核为重启塞回的调用号。glibc 从头到尾都没有重新发起调用、程序自己数的 retries=0 与它互证,用户态从头到尾见到的都不是 EINTR。

请您留意这里的判别难题:光看 read 序列的形状,norestart 的用户态重试与 restart 的内核重启,长得是一模一样的。想分清重试的到底是谁,得让程序自己数着重试的次数,再拿 rt_sigreturn 右侧的值互证、两份证据对上才算数。咱们把计数器做进实验,就是为了这口气。

最后补一条 man 7 signal 的清单,它划的是 SA_RESTART 的边界:就算设了它,poll、ppoll、select、epoll_wait、nanosleep、clock_nanosleep 的这批接口被打断时照样以 EINTR 失败,多路复用和定时类的调用没法安全地从头再来,man 把它们列成了 SA_RESTART 管不到的一类。实验里的 `nap()` 正好踩中清单里的 nanosleep:它被信号打断了,剩下没睡完的部分,会通过剩余时间参数告诉咱们,补睡的活只能调用方自己干:

```cpp
static void nap(long ms)
{
    struct timespec ts{0, ms * 1000000L};
    struct timespec rem{};
    while (nanosleep(&ts, &rem) == -1 && errno == EINTR) {
        ts = rem;   // 补足没睡完的部分:nanosleep 不吃 SA_RESTART
    }
}
```

## 双出口:工具层 expected,应用顶层 system_error

错误到了 error_code 这一层,已经是能拷贝、能比较、能产文本的普通值,接下来怎么往上送?咱们这个系列定下的约定只有一句话:工具层,也就是可能失败的函数和库,返回的是 `std::expected<值, std::error_code>`、错误在链上短路。而应用顶层,也就是 main 附近失败即致命的那一层,把 error_code 包成 `std::system_error` 的异常抛出。全程序的 throw 点都收敛到顶层,中间的各层只见值、不见异常。

为什么不让异常一路抛?每层都 try 一遍的代码您写过就知道,几层下来 catch 的噪音比逻辑还多,而且系统编程的代码,经常要跟 C 接口、线程入口这些不方便异常穿墙的边界打交道。那顶层为什么又用异常?顶层的失败要报告给人,system_error 自带 what() 和携带的 code,catch 站点的语义是现成的。error_code 与 expected 的完整体系,vol3 的 [error_code](../../../vol3-standard-library/error-utils/66-error-code.md) 和 [expected](../../../vol3-standard-library/error-utils/64-expected.md) 两篇从零讲过,咱们这里只取用。

C++23 的硬边界也交代在前面,免得您拿 -std=c++20 编译时踩中它:实测 g++ 16.2.1 下,`<expected>` 头文件在 C++20 里是能 include 的,一用上 std::expected 的本体,编译器报的就是 `'std::expected' is only available from C++23 onwards`。所以本篇的代码分两档语言标准:errno 装箱与读取时机的实验按 C++20 编译,EINTR 与 expected 链的实验按 C++23 编译,您抄命令的时候,标准档可别拿错了。

E3 的链,咱们从最底层往上摞。L0 是 open 的唯一出口,fd 直接装进前一篇的 unique_fd(实验文件里附了一份等价实现,咱们省去),所以谁也不抛:

```cpp
// L0:open 的唯一出口。成功给 fd,失败给 errno 装箱
static std::expected<unique_fd, std::error_code> open_checked(const char* path)
{
    int fd = ::open(path, O_RDONLY);
    if (fd == -1)
        return std::unexpected(errno_code());
    return unique_fd{fd};
}
```

L1 的实现体是个读循环、部分读是常态,咱们得用循环兜住它,EINTR 的重试就地在循环里做、做法与 sys_call 一致。真正的新东西在外壳,and_then 把打开和读完串成了一条链,fd 的移动语义照常工作:

```cpp
// L1 的实现体:循环读到 EOF,EINTR 就地重试
static std::expected<std::string, std::error_code> drain(unique_fd fd)
{
    std::string out;
    char buf[256];
    for (;;) {
        ssize_t n = ::read(fd.get(), buf, sizeof buf);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return std::unexpected(errno_code());
        }
        if (n == 0)
            return out;  // EOF
        out.append(buf, static_cast<size_t>(n));
    }
}

// L1:and_then 把"打开 -> 读完"串成一条链
static std::expected<std::string, std::error_code> read_text(const char* path)
{
    return open_checked(path).and_then(drain);
}
```

L2 咱们再叠一层解析。空文件的情况不是 syscall 错误、`errc::invalid_argument` 一样能进链,这一层想跟您说明的是,expected 的错误通道不挑出身:

```cpp
// L2:空文件给 errc::invalid_argument,非 errno 错误同样进链
static std::expected<std::string, std::error_code> first_line(const char* path)
{
    return read_text(path).and_then([](std::string&& text)
                                    -> std::expected<std::string, std::error_code> {
        auto nl = text.find('\n');
        std::string line =
            (nl == std::string::npos) ? std::move(text) : text.substr(0, nl);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return line;
    });
}
```

L3 咱们在错误路径上挂了个 or_else,它只记一行的日志,咱们不改写、也不吞,把错误原样地透传:

```cpp
// L3:or_else 记日志并原样透传
static std::expected<std::string, std::error_code> first_line_logged(const char* path)
{
    return first_line(path).or_else([path](const std::error_code& ec)
                                    -> std::expected<std::string, std::error_code> {
        std::fprintf(stderr, "[config] first_line(\"%s\") failed: %d %s -- passing through\n",
                     path, ec.value(), ec.message().c_str());
        return std::unexpected(ec);
    });
}
```

链走完了,升级只发生在 main、咱们全程序的 throw 点就数它一个:

```cpp
try {
    std::expected<std::string, std::error_code> r = first_line_logged(path);
    if (!r) {
        // 应用顶层:expected 在此升级成异常
        throw std::system_error(r.error(), std::string("config '") + path + "'");
    }
    std::printf("ok: first line = \"%s\"\n", r->c_str());
} catch (const std::system_error& e) {
    std::printf("caught at top: %s\n", e.what());
    std::printf("    code: value = %d, category = %s\n",
                e.code().value(), e.code().category().name());
}
```

咱们把四个场景一次编译全测:正常文件走完链,缺文件、目录、空文件的三种失败,全都原样地传到顶层。目录的场景尤其值得您多看一眼,open 成功、错误生在链中层的 read,EISDIR 从链的中层出发,照样一路穿到了 main:

```text
$ ./e3 /tmp/errpar/e3.conf
loading config from: /tmp/errpar/e3.conf
ok: first line = "resolution = 1920x1080"

$ ./e3 /tmp/errpar/e3_missing.conf
[config] first_line("/tmp/errpar/e3_missing.conf") failed: 2 No such file or directory -- passing through
loading config from: /tmp/errpar/e3_missing.conf
caught at top: config '/tmp/errpar/e3_missing.conf': No such file or directory
    code: value = 2, category = generic
```

(空文件的场景同样全通,错误值是 22 的 invalid_argument,一行不改地穿过了 or_else。目录场景的 21 亦然,您在仓库的输出存档里可以逐行核对)。

Windows 侧咱们定同款约定,那边的 4 号实验(接前篇续排的小写 e4,与 Linux 的 E4 无关)拿 `read_file_size` 演了一遍:句柄交给前一篇的 unique_handle,失败分支的头一行用 `last_error_code()` 装箱,顶层包成了 system_error:

```cpp
std::expected<unsigned long long, std::error_code> read_file_size(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return std::unexpected(last_error_code());  // 失败分支头一行:立刻装箱
    }
    unique_handle guard{h};  // 之后所有 return 路径都自动关句柄
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(guard.get(), &sz)) {
        return std::unexpected(last_error_code());
    }
    return (unsigned long long)sz.QuadPart;
}
```

咱们看顶层的 catch 站点,`e.code()` 里躺着的是 2,`e.code() == errc::no_such_file_or_directory` 的实测值是 1,判等是跨平台可用的。what() 的可读性跟着 category 走,前文那个自定义 category 在这里出力(输出取自存档的 [3] 段,[1] 与 [2] 两段是成功与失败路径的直读,咱们略过):

```text
    [system_category] e.code().value()=2
      what() 原始:  "read_file_size: ϵͳ�Ҳ���ָ�����ļ���"   <-- message 是 ANSI 代码页字节
      what() 转码后:"read_file_size: 系统找不到指定的文件。"
      e.code() == errc::no_such_file_or_directory : 1
    [win32_category] what():"read_file_size: 系统找不到指定的文件。"  <-- UTF-8 直读,无需转码
      e.code() == errc::no_such_file_or_directory : 1
```

## 零开销的边界:汇编说了什么,没说什么

expected 串上 and_then 的写法,读着像每层都要建对象、查状态的样子,代价到底几何?咱们把场景收窄成这样的纯值域:入参进来、可能失败的一步、失败给 -1。咱们写两版做对照:一版手写分支、一版 expected 加 and_then 加 value_or,用 -O2 生成各自的一份汇编:

```cpp
static int manual(int x)
{
    if (x > 0)
        return x * 2;
    return -1;
}

static int monadic(int x)
{
    std::expected<int, std::error_code> e{x};
    auto m = e.and_then([](int v) -> std::expected<int, std::error_code> {
        if (v > 0)
            return v * 2;
        return std::unexpected(std::error_code{
            static_cast<int>(std::errc::invalid_argument), std::generic_category()});
    });
    return m.value_or(-1);
}
```

咱们把两份函数体逐条对 diff,五条指令是完全相同的:

```text
_Z11sink_manuali:
	leal	(%rdi,%rdi), %eax
	testl	%edi, %edi
	movl	$-1, %edx
	cmovle	%edx, %eax
	ret

_Z12sink_monadici:
	leal	(%rdi,%rdi), %eax
	testl	%edi, %edi
	movl	$-1, %edx
	cmovle	%edx, %eax
	ret
```

装箱、category 寻址、monadic 的机制,在内联展开之后就全部蒸发了,编译器看到的,只是一次比较加一条条件搬移的指令序列。零开销的说法,在内联可见的纯值域里,咱们拿到了汇编的实证。

不过边界还是要交代全,不然零开销三个字就只说了一半。咱们实测 `sizeof(std::expected<int, std::error_code>)` 是 24,error_code 的本体是 16。拿它当返回值跨过一条不能内联的函数边界时,x86-64 的调用约定会让装不进寄存器的返回值改走隐藏指针的内存返回,编译器圈子给这个机制记的名字是 sret,结构体返回的缩写。所以零开销的成立是有范围的:内联可见的纯值域里成立,跨不透明的调用边界,它就是一次实打实的内存往返。工具层的函数多半短小,又常落在同一次的编译里,前者覆盖了咱们大部分的场景。后者提醒咱们,别在每一层的接口上都硬传 expected。

到了这儿,咱们在这一卷里管错误的几样东西就齐了。Linux 侧的 `errno_code` 与 `sys_call` 在本篇定义,Windows 侧的 `last_error_code` 同样如此,`check_win32` 模板的住处在 [W01](../windows/file-io/01-win32-file-io.md)。`unique_fd`、`unique_handle` 的唯一定义处都在[前一篇](01-raii-paradigm.md),W01 里用到的也是同一副。装箱即定格、失败分支头一行的取值,是两侧共同的守则。往上传的路子就是:工具层用的是 expected,应用顶层用的是 system_error。后面 [POSIX 文件 I/O](../linux/file-io/01-posix-file-io.md) 的读写循环、[Win32 文件 I/O](../windows/file-io/01-win32-file-io.md) 的句柄管理,用的全是今天这几样,它们在真实 I/O 里怎么出力,您到那两篇里接着看。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="errno(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/errno.3.html"
  />
  <ReferenceItem
    :id="2"
    title="GetLastError function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror"
  />
  <ReferenceItem
    :id="3"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
  />
  <ReferenceItem
    :id="4"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="5"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
  <ReferenceItem
    :id="6"
    title="std::expected"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/utility/expected"
  />
  <ReferenceItem
    :id="7"
    title="std::error_code"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/error_code"
  />
  <ReferenceItem
    :id="8"
    title="System Error Codes (0-499)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-"
  />
  <ReferenceItem
    :id="9"
    title="FormatMessage function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage"
  />
</ReferenceCard>
